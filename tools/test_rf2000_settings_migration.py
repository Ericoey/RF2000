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
    match = re.search(r"^(?:void|uint8_t|int|float) " + name + r"\([^;]*?\)\s*\{", source, re.M)
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
#define bit_isfalse(x,b) (!((x)&(b)))
static void st_generate_step_dir_invert_masks(void) {}
static void probe_configure_invert_mask(bool unused) { (void)unused; }
static void system_flag_wco_change(void) {}
static void limits_init(void) {}
static void spindle_init(void) {}
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
static void old_fixture(unsigned version,float x,float y,float z)
{
  memset(storage,0xff,sizeof(storage));
  cut_before=NO_CUT; writes=0; reports_reset();
  settings=defaults;
  settings.steps_per_mm[0]=x; settings.steps_per_mm[1]=y;
  settings.steps_per_mm[2]=z;
  settings.max_rate[0]=100; settings.max_rate[1]=100; settings.max_rate[2]=150;
  for (unsigned axis=0;axis<3;++axis) settings.acceleration[axis]=10*3600;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  eeprom_put_char(0,version);
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
static void assert_default_units(void)
{
  for (unsigned i=0;i<3;++i) assert(settings.steps_per_mm[i]==20480);
}
int main(void)
{
  _Static_assert(sizeof(settings_t)==85,"AVR settings layout changed");
  _Static_assert(offsetof(settings_t,homing_pulloff)==81,"AVR settings offset changed");
  assert(RF2000_XY_MICROSTEPS==256 && RF2000_Z_MICROSTEPS==256);
  for (unsigned i=0;i<3;++i) assert(defaults.steps_per_mm[i]==20480);
  uint8_t image[4096];

  // Both supported former coordinate systems keep exact calibration ratios.
  for (unsigned version=74;version<=75;++version) {
    const float xy_scale=version==74 ?8.0f:64.0f;
    old_fixture(version,20544.0f/xy_scale,20416.0f/xy_scale,1296);
    settings.max_rate[0]=80; settings.max_rate[1]=90; settings.max_rate[2]=120;
    settings.acceleration[0]=4*3600; settings.acceleration[1]=20*3600;
    settings.acceleration[2]=9*3600;
    memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
    memcpy(image,storage,sizeof(image)); boot_globals();
    assert(settings.steps_per_mm[0]==20544 && settings.steps_per_mm[1]==20416 &&
      settings.steps_per_mm[2]==20736);
    assert(settings.max_rate[0]==80 && settings.max_rate[1]==90 && settings.max_rate[2]==120);
    assert(settings.acceleration[0]==4*3600 &&
      settings.acceleration[1]==defaults.acceleration[1] && settings.acceleration[2]==9*3600);
    assert(!status_errors && !reports && strstr(messages,"->76; adaptive virtual XYZ 1/256"));
    assert(!strstr(messages,"obsolete")); assert_records_equal(image);
    boot_globals();
    assert(settings.steps_per_mm[0]==20544 && settings.steps_per_mm[1]==20416 &&
      settings.steps_per_mm[2]==20736 && !writes);
    assert(!strstr(messages,"->76")); assert_records_equal(image);
  }

  // Preserve the actual user limits100/100/150; clamp previously tested3375.
  old_fixture(75,320,320,1280);
  settings.max_rate[0]=3375;
  settings.homing_seek_rate=2000;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  memcpy(image,storage,sizeof(image)); boot_globals(); assert_default_units();
  assert(settings.max_rate[0]==600 && settings.max_rate[1]==100 && settings.max_rate[2]==150);
  assert(settings.homing_seek_rate==1000); assert_records_equal(image);
  old_fixture(74,152.38f,2560,1280);
  settings.max_rate[0]=500;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  memcpy(image,storage,sizeof(image)); boot_globals(); assert_default_units();
  assert(settings.max_rate[0]==500);
  assert(strstr(messages,"X: obsolete 152.380 corrected to 20480"));
  assert(!strstr(messages,"Y: obsolete")); assert_records_equal(image);
  old_fixture(74,152.38f,152.38f,1280); boot_globals();
  assert(strstr(messages,"X: obsolete") && strstr(messages,"Y: obsolete"));

  unsigned migration_writes=0;
  for (unsigned version=74;version<=75;++version) {
    old_fixture(version,version==74?2560:320,version==74?2560:320,1280);
    memcpy(image,storage,sizeof(image)); boot_globals();
    migration_writes=writes;
    assert(migration_writes==sizeof(settings_t)+3);
    for (unsigned cut=0;cut<migration_writes;++cut) {
      memcpy(storage,image,sizeof(storage)); writes=0; cut_before=cut;
      reports_reset(); rf2000_preserve_eeprom_records=0;
      if (!setjmp(power_loss)) {
        assert(read_global_settings());
        assert(!"migration completed despite an armed power cut");
      }
      assert(storage[0]==(cut?RF2000_SETTINGS_MIGRATING_VERSION:version));
      boot_globals(); assert_default_units(); assert_records_equal(image);
      boot_globals(); assert_default_units(); assert(!writes);
    }
  }

  // Every global update has the same transaction marker, including version76.
  old_fixture(75,320,320,1280); boot_globals();
  memcpy(image,storage,sizeof(image));
  for (unsigned cut=0;cut<migration_writes;++cut) {
    memcpy(storage,image,sizeof(storage)); settings=defaults;
    settings.steps_per_mm[0]=20544; writes=0; cut_before=cut;
    if (!setjmp(power_loss)) {
      write_global_settings();
      assert(!"global update completed despite an armed power cut");
    }
    boot_globals(); assert_default_units(); assert_records_equal(image);
  }

  // Known corrupt globals and both old/new incomplete markers retain records.
  for (unsigned version=74;version<=76;++version) {
    old_fixture(version,version==74?2560:version==75?320:20480,
      version==74?2560:version==75?320:20480,version==76?20480:1280);
    memcpy(image,storage,sizeof(image));
    storage[EEPROM_ADDR_GLOBAL+sizeof(settings_t)-1]^=1;
    boot_globals();
    assert(status_errors==1 && reports==1 && strstr(messages,"settings recovered"));
    assert_default_units(); assert_records_equal(image);
  }
  for (unsigned marker=0xa5;marker<=0xa6;++marker) {
    old_fixture(marker,320,320,1280); memcpy(image,storage,sizeof(image));
    boot_globals(); assert(status_errors==1); assert_default_units(); assert_records_equal(image);
  }
  old_fixture(75,NAN,320,1280); memcpy(image,storage,sizeof(image));
  boot_globals(); assert(status_errors==1); assert_default_units(); assert_records_equal(image);

  // Current-profile rate validation uses physical pulses, plus the1000cap.
  old_fixture(76,20480,20480,20480);
  settings.max_rate[0]=settings.max_rate[1]=settings.max_rate[2]=1000;
  memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
  boot_globals(); assert(!status_errors && settings.max_rate[0]==1000);
  assert(settings_store_global_setting(110,1000)==STATUS_OK);
  assert(settings_store_global_setting(111,1001)==STATUS_MAX_STEP_RATE_EXCEEDED);
  assert(settings_store_global_setting(112,1001)==STATUS_MAX_STEP_RATE_EXCEEDED);
  assert(settings_store_global_setting(100,23040)==STATUS_OK); // exactly12kHz at1000
  assert(settings_store_global_setting(100,23041)==STATUS_MAX_STEP_RATE_EXCEEDED);
  assert(settings_store_global_setting(24,1000)==STATUS_OK);
  assert(settings_store_global_setting(25,1001)==STATUS_MAX_STEP_RATE_EXCEEDED);
  assert(settings_store_global_setting(24,0)==STATUS_MAX_STEP_RATE_EXCEEDED);
  assert(settings_store_global_setting(110,NAN)==STATUS_BAD_NUMBER_FORMAT);
  assert(settings.max_rate[1]==1000 && settings.max_rate[2]==1000);
  for (unsigned which=0;which<3;++which) {
    settings=defaults;
    if (!which) settings.max_rate[0]=1001;
    else if (which==1) settings.homing_seek_rate=1001;
    else settings.homing_feed_rate=1001;
    memcpy_to_eeprom_with_checksum(EEPROM_ADDR_GLOBAL,(char*)&settings,sizeof(settings));
    assert(!read_global_settings());
  }

  // Unknown foreign profile still uses complete initialization/isolation.
  old_fixture(73,320,320,1280); boot_globals();
  assert(status_errors==1); assert_default_units();
  char line[EEPROM_LINE_SIZE];
  assert(settings_read_startup_line(0,line) && !line[0]);
  assert(settings_read_build_info(line) && !line[0]);
  float coordinates[3]; assert(settings_read_coord_data(0,coordinates));
  assert(!coordinates[0] && !coordinates[1] && !coordinates[2]);
  // Actual production nominal-speed function caps diagonal/overridden paths.
  plan_block_t block={.programmed_rate=1732.0f,.rapid_rate=1732.0f,.condition=0};
  sys.f_override=200; sys.r_override=100;
  assert(plan_compute_profile_nominal_speed(&block)==1000);
  block.condition=PL_COND_FLAG_RAPID_MOTION;
  assert(plan_compute_profile_nominal_speed(&block)==1000);
  block.condition=PL_COND_FLAG_NO_FEED_OVERRIDE;
  assert(plan_compute_profile_nominal_speed(&block)==1000);
  block.condition=0; block.programmed_rate=500; block.rapid_rate=600;
  assert(plan_compute_profile_nominal_speed(&block)==600);
  sys.f_override=50;
  assert(plan_compute_profile_nominal_speed(&block)==250);
  puts("PASS: actual planner nominal profile caps diagonal, rapid and override path feeds at1000 while preserving lower axis limits");
  printf("PASS: actual CNC74/75->76 migration, fine calibration, old152.380 correction, conservative retained limits and independent records\n");
  printf("PASS: power cut before each of%u writes for BOTH former versions and%u current-profile writes; no repeat conversion; A5/A6/corrupt recovery keeps offsets\n",
    migration_writes,migration_writes);
  puts("PASS: actual setting writes/readback enforce1000mm/min axis/homing caps and12kHz effective physical pulse budget");
  puts("PASS: AVR85-byte packed layout/checksum, repeated boots, foreign-profile isolation; ASAN/UBSAN enabled");
  return 0;
}
'''

statuses = "\n".join(re.findall(r"^#define STATUS_.*$", (ROOT/"grbl/report.h").read_text(), re.M))
source = mock + statuses + "\n" + profile + header + '\n#include "defaults.h"\n'
source += settings[globals_start:globals_end]
source += function(eeprom, "memcpy_to_eeprom_with_checksum")
source += function(eeprom, "memcpy_from_eeprom_with_checksum")
for name in ("settings_store_startup_line", "settings_store_build_info",
             "settings_write_coord_data", "write_global_settings", "settings_restore",
             "settings_read_startup_line", "settings_read_build_info", "settings_read_coord_data",
             "read_global_settings", "settings_store_global_setting", "settings_init"):
    source += function(settings, name)
source += "\n".join(re.findall(r"^#define PL_COND_FLAG_.*$", (ROOT/"grbl/planner.h").read_text(), re.M)) + "\n"
source += re.search(r"^#define MINIMUM_FEED_RATE .*$", config, re.M).group() + "\n"
source += "typedef struct { float programmed_rate, rapid_rate; uint8_t condition; } plan_block_t;\n"
source += "static struct { uint8_t f_override, r_override; } sys;\n"
source += function((ROOT/"grbl/planner.c").read_text(), "plan_compute_profile_nominal_speed")
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
