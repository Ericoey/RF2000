#!/usr/bin/env python3
"""Exercise the actual driver state machine with a host GPIO/register model.

Only the SPI transport is substituted; the production initialization, register
profiles, motor masking, timing decisions and fault handling are compiled from
grbl/rf2000_drv8711.c unchanged. The separate simavr test executes the transport
and AVR instructions as well. This test does not establish physical safety.
"""
from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

MOCK_HEADER = r"""
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define CPU_MAP_2560_RENKFORCE_CTC_V3
#define N_AXIS 3
#define _BV(x) (1u << (x))
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(x) for (int once=1; once; once=0)
#define INT6 6
#define INTF6 6
#define ISC60 4
#define ISC61 5
#define INT6_vect mock_int6_vector
#define ISR(vector) void vector(void)
static uint8_t PORTA,PORTB,PORTC,PORTD,PORTE,PORTF,PORTG,PORTJ,PORTK,PORTL;
static uint8_t DDRA,DDRB,DDRC,DDRD,DDRE,DDRF,DDRG,DDRJ,DDRK,DDRL,PINE;
static uint8_t EIMSK,EICRB,EIFR;
static unsigned waited_ms, reset_count;
static bool initializing, fault_during_enable;
static void mock_delay_ms(unsigned n);
#define _delay_ms mock_delay_ms
static void _delay_us(unsigned n) {(void)n;}
"""
for port in "ABCDEFGHJKL":
    for bit in range(8):
        MOCK_HEADER += f"#define P{port}{bit} {bit}\n"

MOCK_TRANSFER = r"""
static uint16_t mock_registers[5][8];
static int drop_driver = -1, corrupt_after_reset_driver = -1;
static int drop_final_driver = -1, persistent_status_driver = -1;
static uint8_t physical_awake(void)
{
  return ((PORTD >> PD7) & 1) | (((PORTF >> PF2) & 1) << 1) |
    (((PORTK >> PK0) & 1) << 2) | (((PORTA >> PA2) & 1) << 3) |
    (((PORTC >> PC7) & 1) << 4);
}
static uint16_t transfer(uint8_t driver, uint16_t command)
{
  uint8_t address = (command >> 12) & 7;
  assert(driver < 5);
  if (command & 0x8000) {
    uint16_t observed = mock_registers[driver][address];
    return address == 1 ? observed & ~0x0400u : observed;
  }
  if (driver == drop_driver) return 0;
  if (reset_count && driver == corrupt_after_reset_driver) return 0;
  if (reset_count && address == 0 && (command & 1) &&
      !physical_awake() && driver == drop_final_driver) return 0;
  if (address == 7) {
    uint16_t keep_uvlo = (physical_awake() & (1u << driver)) ? 0 :
      mock_registers[driver][7] & 0x20;
    mock_registers[driver][7] &= command & 0xff;
    mock_registers[driver][7] |= keep_uvlo;
    if (driver == persistent_status_driver) mock_registers[driver][7] |= 1;
  } else mock_registers[driver][address] = command & 0xfff;
  return 0;
}
"""

TEST_BODY = r"""
static void mock_delay_ms(unsigned n)
{
  waited_ms += n;
  if (initializing) {
    /* Any waking during startup requires ENBL=0 on every awake channel. */
    for (unsigned d=0; d<5; ++d)
      assert(!(physical_awake() & (1u << d)) || !(mock_registers[d][0] & 1));
    assert(!(PORTF & (_BV(PF0)|_BV(PF3)|_BV(PF6))));
    assert(!(PORTA & _BV(PA4))); assert(!(PORTC & _BV(PC1)));
  }
  if (PORTE & _BV(PE7)) {
    assert(initializing && physical_awake() == 0x1f);
    ++reset_count;
    for (unsigned d=0; d<5; ++d) {
      for (unsigned a=0; a<8; ++a) mock_registers[d][a] = 0;
      mock_registers[d][0] = 0xc10; /* TI reset default: ENBL=0. */
      mock_registers[d][7] = 0x20; /* Model startup UVLO, clear only awake. */
    }
  }
  if (!initializing && fault_during_enable) {
    fault_during_enable = false;
    PINE = 0;
    assert(rf2000_drivers_fault_active()); /* Simulate an emergency ISR. */
  }
}
static void fixture(void)
{
  initialized=ready=fault_reason=awake_mask=readback_failed_mask=0;
  PORTA=PORTB=PORTC=PORTD=PORTE=PORTF=PORTG=PORTJ=PORTK=PORTL=0;
  DDRA=DDRB=DDRC=DDRD=DDRE=DDRF=DDRG=DDRJ=DDRK=DDRL=0;
  EIMSK=EICRB=EIFR=0;
  PINE=_BV(PE6); waited_ms=reset_count=0;
  drop_driver=corrupt_after_reset_driver=drop_final_driver=persistent_status_driver=-1;
  initializing=true; fault_during_enable=false;
  for (unsigned d=0; d<5; ++d) {
    for (unsigned a=0; a<8; ++a) mock_registers[d][a]=0;
    /* A previous running firmware can retain ENBL=1 across MCU-only reset. */
    mock_registers[d][0]=0xe29;
    mock_registers[d][7]=0x20;
  }
}
static void boot(void)
{
  rf2000_drivers_init();
  initializing=false;
}
static void assert_asleep(void)
{
  assert(physical_awake()==0);
}
int main(void)
{
  fixture(); boot();
  assert(rf2000_drivers_ready()); assert(!rf2000_drivers_fault_active()); assert_asleep();
  assert(reset_count==1 && waited_ms==35);
  assert((EICRB & (_BV(ISC61)|_BV(ISC60))) == _BV(ISC61));
  assert(EIMSK & _BV(INT6));
  const uint16_t xy_control = RF2000_CNC ? 0xe11 : 0xe29;
  assert(RF2000_XY_MICROSTEPS == (RF2000_CNC ? 4 : 32));
  assert(RF2000_Z_MICROSTEPS == 16);
  assert(mock_registers[0][0]==xy_control && mock_registers[1][0]==xy_control);
  assert(mock_registers[2][0]==0xe21 && mock_registers[3][0]==0xe28 && mock_registers[4][0]==0xe28);
  assert(mock_registers[0][1]==0x16e && mock_registers[1][1]==0x16e);
  assert(mock_registers[2][1]==0x169 && mock_registers[3][1]==0x15a && mock_registers[4][1]==0x15a);
  for (unsigned d=0; d<5; ++d) {
    assert(rf2000_drivers_initial_status(d)==0);
    assert(mock_registers[d][2]==0x97 && mock_registers[d][3]==0x1d7);
    assert(mock_registers[d][4]==0x430 && mock_registers[d][5]==0x83c && mock_registers[d][6]==0xf0);
  }
  assert(rf2000_drivers_initial_status(5)==0xffff);
  unsigned before=waited_ms; rf2000_drivers_enable(0x1f);
  assert(waited_ms==before+25 && physical_awake()==7);
  assert(!(PORTA&(_BV(PA2)|_BV(PA4)))); assert(!(PORTC&(_BV(PC7)|_BV(PC1))));
  before=waited_ms; rf2000_drivers_enable(7); assert(waited_ms==before);
  rf2000_drivers_disable(); assert_asleep();
  rf2000_drivers_enable(7); PINE=0;
  assert(rf2000_drivers_fault_active()); assert_asleep();
  PINE=_BV(PE6); rf2000_drivers_init(); rf2000_drivers_enable(7);
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_fault_active()); assert_asleep();

  fixture(); drop_driver=4; boot();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_readback_failed_mask()==0x10);
  assert(rf2000_drivers_fault_reason() & RF2000_DRIVER_FAULT_READBACK);
  assert(waited_ms==0 && reset_count==0); /* Never wake after failed precheck. */
  assert(!(EIMSK & _BV(INT6)));
  rf2000_drivers_enable(7); assert_asleep();

  fixture(); corrupt_after_reset_driver=3; boot();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_readback_failed_mask()==8);
  assert(!(EIMSK & _BV(INT6)));
  assert_asleep();

  fixture(); drop_final_driver=0; boot();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_readback_failed_mask()==1);
  assert(!(EIMSK & _BV(INT6)));
  assert_asleep();

  fixture(); persistent_status_driver=1; boot();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_fault_reason() & RF2000_DRIVER_FAULT_STATUS);
  assert(rf2000_drivers_initial_status(1)==1); assert_asleep();

  fixture(); PINE=0; boot();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_fault_reason() & RF2000_DRIVER_FAULT_PIN);
  assert_asleep();

  fixture(); boot(); fault_during_enable=true; rf2000_drivers_enable(7);
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_fault_active()); assert_asleep();
  PINE=_BV(PE6); rf2000_drivers_enable(7); assert_asleep();

  fixture(); boot(); rf2000_drivers_enable(7);
  /* A falling edge remains pending even when the line recovered before ISR. */
  PINE=_BV(PE6); mock_int6_vector();
  assert(!rf2000_drivers_ready()); assert(rf2000_drivers_fault_active()); assert_asleep();
  rf2000_drivers_init(); rf2000_drivers_enable(7); assert_asleep();
  puts("PASS: actual DRV8711 state machine; all five profiles, awake reset/UVLO clear,");
  puts("XYZ-only enable, reserve ENBL=0, wake timing, readback failures at all phases,");
  puts("persistent STATUS/FAULT, sticky soft-reset latch, fault during enable,");
  puts("INT6 falling-edge configuration and short recovered FAULT pulse latch.");
}
"""


def main():
    config = (ROOT / "grbl/config.h").read_text()
    profile = config.split("/* RF2000_MICROSTEP_PROFILE_BEGIN:", 1)[1]
    profile = profile[profile.index("*/") + 2:]
    profile = profile.split("/* RF2000_MICROSTEP_PROFILE_END */", 1)[0]
    source = (ROOT / "grbl/rf2000_drv8711.c").read_text()
    source = source.replace('#include "grbl.h"', '#include "mock_avr.h"')
    source = source.replace('#include <util/atomic.h>', "")
    start = source.index("static uint16_t transfer(")
    end = source.index("\nstatic uint16_t read_register", start)
    source = source[:start] + MOCK_TRANSFER + source[end:]
    with tempfile.TemporaryDirectory(prefix="rf2000-driver-test-") as temporary:
        directory = Path(temporary)
        (directory / "mock_avr.h").write_text(MOCK_HEADER + profile)
        harness = directory / "driver_test.c"
        harness.write_text(source + TEST_BODY)
        binary = directory / "driver_test"
        for cnc in (0, 1):
            print(f"DRV8711 {'CNC 1/4' if cnc else 'legacy/test 1/32'} profile", flush=True)
            subprocess.run([
                os.environ.get("HOST_CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                f"-DRF2000_CNC={cnc}", "-Wno-unused-function", f"-I{ROOT / 'grbl'}",
                str(harness), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
