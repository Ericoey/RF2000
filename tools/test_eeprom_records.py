#!/usr/bin/env python3
"""Compile and exercise actual EEPROM record functions with host-side storage.

Function bodies are loaded from the firmware sources. AVR register effects and
the physical EEPROM are mocked; no firmware algorithm is reimplemented here.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
settings = (root / "grbl/settings.c").read_text()
eeprom = (root / "grbl/eeprom.c").read_text()
header = (root / "grbl/settings.h").read_text()
config = (root / "grbl/config.h").read_text()


def function(source, name):
    match = re.search(r"^(?:void|uint8_t|int) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    depth = 1
    end = match.end()
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end] + "\n"


constants = ""
for name in ("EEPROM_LINE_SIZE", "EEPROM_ADDR_STARTUP_BLOCK", "EEPROM_ADDR_BUILD_INFO"):
    match = re.search(r"^\s*#define " + name + r"\s+(\d+U?)", header, re.M)
    assert match, name
    constants += f"#define {name} {match[1]}\n"
match = re.search(r"^#define N_STARTUP_LINE\s+(\d+)", config, re.M)
assert match
constants += f"#define N_STARTUP_LINE {match[1]}\n"

mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define N_AXIS 3
#define SETTING_INDEX_NCOORD 7
#define SETTINGS_RESTORE_DEFAULTS 1
#define SETTINGS_RESTORE_PARAMETERS 2
#define SETTINGS_RESTORE_STARTUP_LINES 4
#define SETTINGS_RESTORE_BUILD_INFO 8
typedef struct { int dummy; } settings_t;
static settings_t settings, defaults;
static void write_global_settings(void) { }
static void settings_write_coord_data(uint8_t n, float *data) { (void)n; (void)data; }
static unsigned char storage[4096];
static unsigned writes;
static void eeprom_put_char(unsigned int addr, unsigned char value) {
  assert(addr < sizeof(storage)); storage[addr] = value; writes++;
}
static unsigned char eeprom_get_char(unsigned int addr) {
  assert(addr < sizeof(storage)); return storage[addr];
}
'''

hardware_mock = r'''
static uint8_t SREG, EECR, EEDR;
static uint16_t EEAR;
#define EEPE 1
#define EEMPE 2
#define EERE 0
#define EEPM1 5
#define EEPM0 4
#define EEPROM_IGNORE_SELFPROG
#define cli() (SREG &= (uint8_t)~0x80)
#define eeprom_put_char eeprom_hw_put_char
'''

tests = r'''
int main(void) {
  _Static_assert(EEPROM_ADDR_STARTUP_BLOCK + N_STARTUP_LINE*(EEPROM_LINE_SIZE+1) <=
                 EEPROM_ADDR_BUILD_INFO, "startup and build-info overlap");
  char line[EEPROM_LINE_SIZE + 2];
  char too_long[EEPROM_LINE_SIZE + 1];
  memset(too_long, 'Z', sizeof(too_long)-1); too_long[sizeof(too_long)-1] = 0;
  memset(storage, 0xa5, sizeof(storage));
  settings_store_startup_line(0, "G21");
  settings_store_startup_line(1, "G90");
  settings_store_build_info("EEPROMCHECK");
  unsigned written = writes;
  settings_store_startup_line(N_STARTUP_LINE, "G20");
  settings_store_startup_line(255, "G20");
  settings_store_startup_line(0, too_long);
  settings_store_build_info(too_long);
  assert(writes == written);
  memset(line, 0x5a, sizeof(line));
  assert(settings_read_startup_line(0, line) && !strcmp(line, "G21"));
  assert((unsigned char)line[EEPROM_LINE_SIZE] == 0x5a);
  assert(settings_read_startup_line(1, line) && !strcmp(line, "G90"));
  assert(settings_read_build_info(line) && !strcmp(line, "EEPROMCHECK"));
  assert(settings_read_startup_line(0, line) && !strcmp(line, "G21"));
  assert(settings_read_startup_line(1, line) && !strcmp(line, "G90"));
  assert(!settings_read_startup_line(255, line) && !line[0]);
  assert(writes == written);
  // Exactly 79 characters plus NUL must survive; no truncation.
  too_long[EEPROM_LINE_SIZE-1] = 0;
  settings_store_startup_line(0, too_long);
  assert(settings_read_startup_line(0, line));
  assert(strlen(line) == EEPROM_LINE_SIZE-1 && !strcmp(line, too_long));
  // A matching checksum without any terminator is still an invalid record.
  memset(too_long, 'A', EEPROM_LINE_SIZE);
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_STARTUP_BLOCK, too_long, EEPROM_LINE_SIZE);
  assert(!settings_read_startup_line(0, line) && !line[0]);
  assert(settings_read_startup_line(0, line) && !line[0]);
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_BUILD_INFO, too_long, EEPROM_LINE_SIZE);
  assert(!settings_read_build_info(line) && !line[0]);
  assert(settings_read_build_info(line) && !line[0]);
  // Restore must use the identical record layout and valid empty checksums.
  settings_restore(SETTINGS_RESTORE_STARTUP_LINES | SETTINGS_RESTORE_BUILD_INFO);
  assert(settings_read_startup_line(0, line) && !line[0]);
  assert(settings_read_startup_line(1, line) && !line[0]);
  assert(settings_read_build_info(line) && !line[0]);
  for (unsigned i=0; i<sizeof(storage); i++) {
    bool startup = i >= EEPROM_ADDR_STARTUP_BLOCK &&
      i < EEPROM_ADDR_STARTUP_BLOCK + N_STARTUP_LINE*(EEPROM_LINE_SIZE+1);
    bool info = i >= EEPROM_ADDR_BUILD_INFO && i <= EEPROM_ADDR_BUILD_INFO + EEPROM_LINE_SIZE;
    if (!startup && !info) assert(storage[i] == 0xa5);
  }
  // Exercise the actual register-write function with I=0 and I=1, covering
  // unchanged, erase-only, write-only and erase+write branches.
  const uint8_t old_values[] = {0xff, 0, 0xff, 0x55};
  const uint8_t new_values[] = {0xff, 0xff, 0, 0xaa};
  for (unsigned enabled=0; enabled<2; enabled++) {
    for (unsigned n=0; n<4; n++) {
      SREG = enabled ? 0xb5 : 0x35;
      EECR = 0; EEDR = old_values[n]; EEAR = 0xffff;
      eeprom_hw_put_char(13, new_values[n]);
      assert(SREG == (enabled ? 0xb5 : 0x35));
      assert(EEAR == 13);
    }
  }
  puts("EEPROM record separation, length/index guards, NUL handling, restore and SREG: PASS");
  return 0;
}
'''

source = mock + constants
source += function(eeprom, "memcpy_to_eeprom_with_checksum")
source += function(eeprom, "memcpy_from_eeprom_with_checksum")
for name in ("settings_store_startup_line", "settings_store_build_info", "settings_restore",
             "settings_read_startup_line", "settings_read_build_info"):
    source += function(settings, name)
source += hardware_mock + function(eeprom, "eeprom_put_char") + "\n#undef eeprom_put_char\n" + tests
with tempfile.TemporaryDirectory(prefix="rf2000-eeprom-") as directory:
    directory = Path(directory)
    cfile = directory / "eeprom_records.c"
    binary = directory / "eeprom_records"
    cfile.write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
