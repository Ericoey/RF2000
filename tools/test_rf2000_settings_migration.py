#!/usr/bin/env python3
"""Exercise production CNC settings migration, including every write boundary.

The EEPROM transport/reporters are substituted. The settings structure is
packed to AVR's byte alignment and checked against its measured 85-byte layout;
production record/checksum and migration function bodies are compiled unchanged.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
settings = (ROOT / "grbl/settings.c").read_text()
header = (ROOT / "grbl/settings.h").read_text()
config = (ROOT / "grbl/config.h").read_text()
eeprom = (ROOT / "grbl/eeprom.c").read_text()


def function(source, name):
    match = re.search(r"^(?:void|uint8_t|int) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end] + "\n"


profile = config.split("/* RF2000_MICROSTEP_PROFILE_BEGIN:", 1)[1]
profile = profile[profile.index("*/") + 2:]
profile = profile.split("/* RF2000_MICROSTEP_PROFILE_END */", 1)[0]
header = header.replace('#include "grbl.h"', "")
header = header.replace("typedef struct {", "typedef struct __attribute__((packed)) {")
globals_start = settings.index("settings_t settings;")
globals_end = settings.index("// Method to store startup lines", globals_start)

mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stddef.h>
#define CPU_MAP_2560_RENKFORCE_CTC_V3
#define RF2000_CNC 1
#define RF2000_Y_HOMING_TEST 0
#define RF2000_HOMING_VERIFIED 0
#define DEFAULTS_RENKFORCE_RF2000V2
#define N_AXIS 3
#define AXIS_1 0
#define AXIS_2 1
#define AXIS_3 2
#define N_STARTUP_LINE 2
#define MAX_STEP_RATE_HZ 18000
#define bit(n) (1u << (n))
#define __flash
#define PSTR(s) (s)
#define clear_vector_float(data) memset(data, 0, sizeof(float)*N_AXIS)
#define STATUS_SETTING_READ_FAIL 7
#define NO_CUT (~0u)
static uint8_t storage[4096];
static unsigned writes, cut_before=NO_CUT;
static jmp_buf power_loss;
static char messages[2048];
static unsigned status_errors, reports;
static void eeprom_put_char(unsigned int addr, unsigned char value)
{
  assert(addr < sizeof(storage));
  if (writes == cut_before) longjmp(power_loss, 1);
  storage[addr]=value; ++writes;
}
static unsigned char eeprom_get_char(unsigned int addr)
{
  assert(addr < sizeof(storage)); return storage[addr];
}
static void printPgmString(const char *s)
{
  assert(strlen(messages)+strlen(s)<sizeof(messages)); strcat(messages,s);
}
static void print_uint8_base10(uint8_t n)
{
  char text[4]; snprintf(text,sizeof(text),"%u",n); printPgmString(text);
}
static void printFloat(float n,uint8_t decimals)
{
  char text[32]; snprintf(text,sizeof(text),"%.*f",decimals,(double)n); printPgmString(text);
}
static void report_status_message(uint8_t code)
{
  assert(code==STATUS_SETTING_READ_FAIL); ++status_errors;
}
static void report_grbl_settings(void) { ++reports; }
static uint8_t is_report_grbl_settings_running(void) { return 0; }
'''

tests = r'''
static void reports_reset(void)
{
  messages[0]=0; status_errors=reports=0;
}
static void boot_globals(void)
{
  cut_before=NO_CUT; writes=0; reports_reset();
  rf2000_preserve_eeprom_records=0;
  memset(&settings,0xa7,sizeof(settings));
  settings_init();
  assert(storage[0]==SETTINGS_VERSION && read_global_settings());
}
static void old_fixture(float x,float y)
{
  memset(storage,0xff,sizeof(storage));
  cut_before=NO_CUT; writes=0; reports_reset();
  settings=defaults;
  settings.steps_per_mm[0]=x; settings.steps_per_mm[1]=y;
  settings.max_rate[0]=100; settings.max_rate[1]=100; settings.max_rate[2]=150;
  for (unsigned axis=0;axis<3;++axis) settings.acceleration[axis]=10*3600;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  eeprom_put_char(0,RF2000_SETTINGS_PREVIOUS_VERSION);
  float coordinates[3]={-1.25f,2.5f,-3.0f};
  for (unsigned n=0;n<=SETTING_INDEX_NCOORD;++n) {
    coordinates[0]=(float)n-1.25f;
    settings_write_coord_data(n,coordinates);
  }
  char n0[]="G21G90G54",n1[]="G17",build[]="RETAINED_RF2000";
  settings_store_startup_line(0,n0); settings_store_startup_line(1,n1);
  settings_store_build_info(build);
  writes=0; reports_reset();
}
static void assert_records_equal(const uint8_t *image)
{
  // No coordinate, startup, build-info, gap or unrelated byte may change.
  const unsigned after_global=EEPROM_ADDR_GLOBAL+sizeof(settings_t)+1;
  assert(!memcmp(storage+after_global,image+after_global,sizeof(storage)-after_global));
  char line[EEPROM_LINE_SIZE];
  assert(settings_read_startup_line(0,line) && !strcmp(line,"G21G90G54"));
  assert(settings_read_startup_line(1,line) && !strcmp(line,"G17"));
  assert(settings_read_build_info(line) && !strcmp(line,"RETAINED_RF2000"));
  float coordinates[3];
  assert(settings_read_coord_data(0,coordinates));
  assert(coordinates[0]==-1.25f && coordinates[1]==2.5f && coordinates[2]==-3.0f);
}
int main(void)
{
  _Static_assert(sizeof(settings_t)==85,"AVR settings layout changed");
  _Static_assert(offsetof(settings_t,homing_pulloff)==81,"AVR settings offset changed");
  assert(RF2000_XY_MICROSTEPS==4 && RF2000_Z_MICROSTEPS==16);
  assert(defaults.steps_per_mm[0]==320 && defaults.steps_per_mm[1]==320 &&
    defaults.steps_per_mm[2]==1280);
  uint8_t image[4096];

  old_fixture(2568,2552);
  settings.max_rate[0]=80; settings.max_rate[1]=90; settings.max_rate[2]=120;
  settings.acceleration[0]=4*3600; settings.acceleration[1]=20*3600;
  settings.acceleration[2]=9*3600;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  memcpy(image,storage,sizeof(image)); boot_globals();
  assert(settings.steps_per_mm[0]==321 && settings.steps_per_mm[1]==319 &&
    settings.steps_per_mm[2]==1280);
  assert(settings.max_rate[0]==80 && settings.max_rate[1]==90 && settings.max_rate[2]==120);
  assert(settings.acceleration[0]==4*3600 &&
    settings.acceleration[1]==fminf(20*3600,defaults.acceleration[1]) &&
    settings.acceleration[2]==9*3600);
  assert(!status_errors && !reports && strstr(messages,"74->75; X/Y microsteps 32->4"));
  assert(!strstr(messages,"obsolete")); assert_records_equal(image);
  boot_globals();
  assert(settings.steps_per_mm[0]==321 && settings.steps_per_mm[1]==319 && !writes);
  assert(!strstr(messages,"74->75")); assert_records_equal(image);

  old_fixture(152.38f,2560);
  settings.max_rate[0]=500;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  memcpy(image,storage,sizeof(image)); boot_globals();
  assert(settings.steps_per_mm[0]==320 && settings.steps_per_mm[1]==320);
  assert(settings.max_rate[0]==fminf(500,defaults.max_rate[0]));
  assert(strstr(messages,"X: obsolete 152.380 corrected to 320"));
  assert(!strstr(messages,"Y: obsolete")); assert_records_equal(image);
  old_fixture(152.38f,152.38f); boot_globals();
  assert(strstr(messages,"X: obsolete") && strstr(messages,"Y: obsolete"));

  old_fixture(2560,2560);
  memcpy(image,storage,sizeof(image)); boot_globals();
  const unsigned migration_writes=writes;
  assert(migration_writes==sizeof(settings_t)+3);
  for (unsigned cut=0;cut<migration_writes;++cut) {
    memcpy(storage,image,sizeof(storage)); writes=0; cut_before=cut;
    reports_reset(); rf2000_preserve_eeprom_records=0;
    if (!setjmp(power_loss)) {
      assert(read_global_settings());
      assert(!"migration reached completion despite an armed power cut");
    }
    if (cut) assert(storage[0]==RF2000_SETTINGS_MIGRATING_VERSION);
    else assert(storage[0]==RF2000_SETTINGS_PREVIOUS_VERSION);
    boot_globals();
    assert(settings.steps_per_mm[0]==320 && settings.steps_per_mm[1]==320 &&
      settings.steps_per_mm[2]==1280);
    assert_records_equal(image);
    boot_globals();
    assert(settings.steps_per_mm[0]==320 && settings.steps_per_mm[1]==320 && !writes);
  }

  // A fresh version75 global update has the same transactional guarantee.
  old_fixture(2560,2560); boot_globals();
  memcpy(image,storage,sizeof(image));
  for (unsigned cut=0;cut<migration_writes;++cut) {
    memcpy(storage,image,sizeof(storage)); settings=defaults;
    settings.steps_per_mm[0]=321; writes=0; cut_before=cut;
    if (!setjmp(power_loss)) {
      write_global_settings();
      assert(!"global update reached completion despite an armed power cut");
    }
    boot_globals();
    assert(settings.steps_per_mm[0]==320 && settings.steps_per_mm[1]==320);
    assert_records_equal(image);
  }

  // Recognized corrupt global records recover only settings, not offsets.
  for (unsigned previous=0;previous<2;++previous) {
    old_fixture(2560,2560);
    if (!previous) boot_globals();
    memcpy(image,storage,sizeof(image));
    storage[EEPROM_ADDR_GLOBAL+sizeof(settings_t)-1]^=1;
    boot_globals();
    assert(status_errors==1 && reports==1 && strstr(messages,"settings recovered"));
    assert(settings.steps_per_mm[0]==320); assert_records_equal(image);
  }
  old_fixture(2560,2560);
  settings.steps_per_mm[0]=NAN;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  memcpy(image,storage,sizeof(image)); boot_globals();
  assert(status_errors==1 && settings.steps_per_mm[0]==320); assert_records_equal(image);

  // A foreign version retains the pre-existing profile-isolation/reset policy.
  old_fixture(2560,2560); storage[0]=73; boot_globals();
  assert(status_errors==1 && settings.steps_per_mm[0]==320);
  char line[EEPROM_LINE_SIZE];
  assert(settings_read_startup_line(0,line) && !line[0]);
  assert(settings_read_build_info(line) && !line[0]);
  float coordinates[3]; assert(settings_read_coord_data(0,coordinates));
  assert(!coordinates[0] && !coordinates[1] && !coordinates[2]);
  printf("PASS: actual CNC settings74->75 migration; fine calibration/legacy152.380 correction, Z unchanged, conservative rates and records retained\n");
  printf("PASS: power cut before each of%u migration writes and each of%u current-profile update writes; no double conversion; known corrupt records recover settings only\n",
    migration_writes,migration_writes);
  puts("PASS: AVR85-byte packed layout/checksum, repeated boots, foreign-profile isolation; ASAN/UBSAN enabled");
  return 0;
}
'''

source = mock + profile + header + '\n#include "defaults.h"\n'
source += settings[globals_start:globals_end]
source += function(eeprom, "memcpy_to_eeprom_with_checksum")
source += function(eeprom, "memcpy_from_eeprom_with_checksum")
for name in ("settings_store_startup_line", "settings_store_build_info",
             "settings_write_coord_data", "write_global_settings", "settings_restore",
             "settings_read_startup_line", "settings_read_build_info", "settings_read_coord_data",
             "read_global_settings", "settings_init"):
    source += function(settings, name)
source += tests

with tempfile.TemporaryDirectory(prefix="rf2000-settings-migration-") as directory:
    directory = Path(directory)
    cfile, binary = directory / "settings_migration.c", directory / "settings_migration"
    cfile.write_text(source)
    subprocess.run([
        os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g", f"-I{ROOT/'grbl'}", str(cfile), "-lm", "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
