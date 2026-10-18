#!/usr/bin/env python3
"""Exercise the actual RF2000 limits backend with host-side AVR GPIO mocks.

This checks input mapping and software safety behavior, not electrical timing.
No copied Python reimplementation of the algorithm is used.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'grbl/limits.c').read_text()
backend = source.split('#ifdef CPU_MAP_2560_RENKFORCE_CTC_V3', 1)[1]
backend = backend.split('#else\nvoid limits_init()', 1)[0]
backend = backend.replace('#include "rf2000_drv8711.h"', '')
mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define bit(n) (1u << (n))
#define PC0 0
#define PC2 2
#define PC6 6
#define PH1 1
#define AXIS_1 0
#define AXIS_2 1
#define AXIS_3 2
#define STATE_IDLE 0
#define STATE_ALARM 1
#define STATE_HOMING 8
#define EXEC_ALARM_HARD_LIMIT 1
#define EXEC_ALARM_DRIVER_FAULT 12
#define WGM21 1
#define CS22 2
#define OCF2A 1
#define OCF2B 2
#define TOV2 0
#define OCIE2A 1
#define F_CPU 16000000UL
#define ISR(name) void name(void)
static uint8_t PINC, PINH, DDRC, DDRH, PORTC, PORTH, SREG;
static uint8_t TIMSK2, TCCR2A, TCCR2B, TCNT2, OCR2A, TIFR2;
static struct { uint8_t state; } sys;
static struct { uint8_t flags; } settings;
static uint8_t sys_rt_exec_alarm;
static unsigned reset_calls, disable_calls;
static bool fault, driver_ready;
static void cli(void) { SREG &= ~0x80; }
static bool rf2000_drivers_fault_active(void) { return fault; }
static bool rf2000_drivers_ready(void) { return driver_ready; }
static void rf2000_drivers_disable(void) { disable_calls++; }
static void mc_reset(void) { reset_calls++; }
static void system_set_exec_alarm(uint8_t n) { sys_rt_exec_alarm = n; }
'''
tests = r'''
static void raw_inputs(uint8_t mask)
{
  PINC = 0xff; PINH = 0xff;
  if (mask & 1) { PINC &= ~bit(PC2); }
  if (mask & 2) { PINC &= ~bit(PC0); }
  if (mask & 4) { PINC &= ~bit(PC6); }
  if (mask & 8) { PINH &= ~bit(PH1); }
}
static void fresh(uint8_t mask)
{
  raw_inputs(mask); sys.state = STATE_IDLE; settings.flags = 0;
  fault = false; driver_ready = true;
  sys_rt_exec_alarm = 0; reset_calls = 0; disable_calls = 0;
  limits_init();
}
int main(void)
{
  DDRC = DDRH = 0xff; PORTC = PORTH = 0; SREG = 0x80;
  fresh(0);
  assert(DDRC == (uint8_t)~(bit(PC2)|bit(PC0)|bit(PC6)));
  assert(DDRH == (uint8_t)~bit(PH1));
  assert(PORTC == (bit(PC2)|bit(PC0)|bit(PC6)) && PORTH == bit(PH1));
  assert(SREG == 0x80 && OCR2A == 249 && TIMSK2 == bit(OCIE2A));
  assert(TCCR2A == bit(WGM21) && TCCR2B == bit(CS22));
  assert(limits_motion_allowed());
  // Every physical port combination, with old EEPROM flag values too.
  for (uint8_t flags = 0; flags < 2; flags++) {
    settings.flags = flags ? 0xff : 0;
    for (uint8_t n = 0; n < 16; n++) {
      raw_inputs(n);
      assert(rf2000_limits_raw_state() == n);
      uint8_t axis = (n & 3) | ((n & 12) ? 4 : 0);
      assert(limits_get_state() == axis);
      assert(limits_motion_allowed() == !n);
    }
  }
  // Active at boot inhibits motion even before the first timer interrupt.
  fresh(8); assert(!limits_motion_allowed());
  sys.state = STATE_ALARM;
  TIMER2_COMPA_vect();
  assert(disable_calls == 1 && reset_calls == 0);
  // Release needs 5 consecutive clean samples; renewed contact restarts it.
  raw_inputs(0);
  for (uint8_t n = 0; n < 4; n++) {
    TIMER2_COMPA_vect(); assert(!limits_motion_allowed());
  }
  raw_inputs(1); TIMER2_COMPA_vect(); raw_inputs(0);
  for (uint8_t n = 0; n < 4; n++) {
    TIMER2_COMPA_vect(); assert(!limits_motion_allowed());
  }
  TIMER2_COMPA_vect(); assert(limits_motion_allowed());
  // A hard limit remains mandatory when all runtime settings are zero.
  fresh(0); raw_inputs(2); TIMER2_COMPA_vect();
  assert(disable_calls == 1 && reset_calls == 1);
  assert(sys_rt_exec_alarm == EXEC_ALARM_HARD_LIMIT);
  // Both fault and failed initialization stop idle machines.
  fresh(0); fault = true; TIMER2_COMPA_vect();
  assert(!limits_motion_allowed() && disable_calls == 1);
  assert(sys_rt_exec_alarm == EXEC_ALARM_DRIVER_FAULT);
  fresh(0); driver_ready = false; TIMER2_COMPA_vect();
  assert(!limits_motion_allowed() && sys_rt_exec_alarm == EXEC_ALARM_DRIVER_FAULT);
  // A future verified homing cycle may consume only its selected switch.
  fresh(0); sys.state = STATE_HOMING; rf2000_homing_expected = 8;
  raw_inputs(8); TIMER2_COMPA_vect();
  assert(limits_get_state() == 4 && limits_motion_allowed());
  assert(disable_calls == 0 && reset_calls == 0);
  raw_inputs(4); TIMER2_COMPA_vect();
  assert(limits_get_state() == 0 && !limits_motion_allowed());
  assert(sys_rt_exec_alarm == EXEC_ALARM_HARD_LIMIT && disable_calls == 1);
  fresh(0); sys.state = STATE_HOMING; rf2000_homing_expected = 1;
  raw_inputs(2); TIMER2_COMPA_vect();
  assert(sys_rt_exec_alarm == EXEC_ALARM_HARD_LIMIT);
  puts("RF2000 limits: 16 input combinations, fixed polarity, init, debounce, hard stop, fault and homing selection PASS");
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='rf2000-limits-') as directory:
    c_file = Path(directory) / 'limits_host.c'
    executable = Path(directory) / 'limits_host'
    c_file.write_text(mock + backend + tests)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                    '-Werror', '-O2', str(c_file), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
