/*
 * RF2000 I851 CTC V3.0 five-channel DRV8711 hardware support.
 * Derived from drv8711Init()/drv8711Transmit()/setMotorCurrent() in the
 * RF Repetier firmware (GPL-3.0-or-later). Part of Grbl.
 * Copyright (c) 2026 RF2000 contributors
 */
#include "grbl.h"
#include "rf2000_drv8711.h"
#include <util/atomic.h>

#ifdef CPU_MAP_2560_RENKFORCE_CTC_V3

#if (RF2000_DRIVER_ACTIVE_MASK & ~((1u << N_AXIS) - 1u))
#error "RF2000 active driver mask refers to an unconfigured Grbl axis"
#endif
#if (RF2000_DRIVER_ACTIVE_MASK & ~0x1fu)
#error "The RF2000 board has only five DRV8711 channels"
#endif

/* All five physical channels exist independently of the Grbl axis count.
 * CTRL: DTIME=3 (850 ns), ISGAIN=2 (20), EXSTALL=0, RDIR=0,
 * RSTEP=0. MODE comes from the same microstep profile as steps/mm: CNC X/Y
 * use 1/4; separate test profiles use 1/32. Z stays 1/16 and A/B stay 1/32.
 * ENBL is set only for configured active channels after safe awake reset. */
static const uint16_t control[RF2000_DRIVER_COUNT] = {
  0x0e00 | (RF2000_DRV8711_MODE(RF2000_XY_MICROSTEPS) << 3) | ((RF2000_DRIVER_ACTIVE_MASK >> 0) & 1),
  0x0e00 | (RF2000_DRV8711_MODE(RF2000_XY_MICROSTEPS) << 3) | ((RF2000_DRIVER_ACTIVE_MASK >> 1) & 1),
  0x0e00 | (RF2000_DRV8711_MODE(RF2000_Z_MICROSTEPS) << 3) | ((RF2000_DRIVER_ACTIVE_MASK >> 2) & 1),
  0x0e00 | (RF2000_DRV8711_MODE(32u) << 3) | ((RF2000_DRIVER_ACTIVE_MASK >> 3) & 1),
  0x0e00 | (RF2000_DRV8711_MODE(32u) << 3) | ((RF2000_DRIVER_ACTIVE_MASK >> 4) & 1)
};
static const uint8_t torque[RF2000_DRIVER_COUNT] = {110, 110, 105, 90, 90};
static const uint16_t register_data[8] = {
  0, 0, 0x0097, 0x01d7, 0x0430, 0x083c, 0x00f0, 0
};
/* Compare documented writable configuration bits, never reserved bits. */
static const uint16_t readback_mask[7] = {
  0x0fff, 0x03ff, 0x01ff, 0x01ff, 0x07ff, 0x0fff, 0x0fff
};

static uint8_t initialized;
static volatile uint8_t ready;
static volatile uint8_t fault_reason;
static volatile uint8_t awake_mask;
static uint8_t readback_failed_mask;
static uint16_t initial_status[RF2000_DRIVER_COUNT];

/* Several of these ports are shared with STEP/DIR. Protect the read/modify/
 * write sequence, including when called inside an ISR with interrupts on.
 * Never hold interrupts off across a transfer or a delay. */
void rf2000_drivers_disable(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTD &= ~_BV(PD7); /* X */
    PORTF &= ~_BV(PF2); /* Y */
    PORTK &= ~_BV(PK0); /* Z */
    PORTA &= ~_BV(PA2); /* A reserve */
    PORTC &= ~_BV(PC7); /* B reserve */
    awake_mask = 0;
  }
}

static void select_driver(uint8_t driver)
{
  /* SCS is ACTIVE HIGH. Only one is selected, especially for reads on the
   * shared SDATO wire. L1 is a direction output, so PORTL needs atomic RMW. */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    switch (driver) {
      case 0: PORTL |= _BV(PL0); break;
      case 1: PORTL |= _BV(PL2); break;
      case 2: PORTB |= _BV(PB6); break;
      case 3: PORTL |= _BV(PL6); break;
      case 4: PORTJ |= _BV(PJ7); break;
    }
  }
  _delay_us(1);
}

static void deselect_drivers(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTL &= ~(_BV(PL0) | _BV(PL2) | _BV(PL6));
    PORTB &= ~_BV(PB6);
    PORTJ &= ~_BV(PJ7);
  }
  _delay_us(1);
}

static uint16_t transfer(uint8_t driver, uint16_t command)
{
  uint16_t received = 0;
  int8_t bit;
  select_driver(driver);
  for (bit = 15; bit >= 0; --bit) {
    /* Constant low-I/O bit accesses compile to atomic SBI/CBI on PORTD,
     * preserving PD7/SLEEPn if an emergency interrupt intervenes. */
    if (command & ((uint16_t)1 << bit)) PORTD |= _BV(PD6);
    else PORTD &= ~_BV(PD6);
    _delay_us(1);
    /* DRV8711 drives SDATO following a falling clock edge. Sample before
     * the next rising edge, as in Repetier's drv8711Receive(). */
    if ((bit < 12) && (PIND & _BV(PD4))) received |= (uint16_t)1 << bit;
    PORTD |= _BV(PD5);
    _delay_us(5);
    PORTD &= ~_BV(PD5);
    _delay_us(1);
  }
  deselect_drivers();
  return received;
}

static uint16_t read_register(uint8_t driver, uint8_t address)
{
  return transfer(driver, 0x8000u | ((uint16_t)address << 12));
}

static void write_register(uint8_t driver, uint8_t address, uint16_t data)
{
  transfer(driver, ((uint16_t)address << 12) | (data & 0x0fff));
}

static uint16_t expected_register(uint8_t driver, uint8_t address)
{
  if (address == 0) return control[driver];
  /* SMPLTH=1 (100 us); TORQUE is an unsigned DAC code, NOT amperes. */
  if (address == 1) return 0x0100u | torque[driver];
  return register_data[address];
}

void rf2000_drivers_init(void)
{
  uint8_t driver, address;
  if (initialized) return;
  initialized = 1;
  ready = 0;
  EIMSK &= ~_BV(INT6); /* Ignore expected startup/reset FAULT transitions. */

  /* Preload inactive output levels BEFORE changing DDR. Keep A/B STEP low,
   * including DRV4's verified PA4 (not OUT2/PA5 from the RF2000 pin map).
   * st_init() subsequently applies the configured XYZ direction polarity. */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTD &= ~(_BV(PD7) | _BV(PD5) | _BV(PD6));
    PORTF &= ~(_BV(PF0) | _BV(PF1) | _BV(PF2) | _BV(PF3) | _BV(PF6) | _BV(PF7));
    PORTL &= ~(_BV(PL0) | _BV(PL1) | _BV(PL2) | _BV(PL6));
    PORTA &= ~(_BV(PA2) | _BV(PA4) | _BV(PA6));
    PORTC &= ~(_BV(PC1) | _BV(PC3) | _BV(PC7));
    PORTK &= ~_BV(PK0);
    PORTB &= ~_BV(PB6);
    PORTJ &= ~_BV(PJ7);
    PORTE &= ~_BV(PE7);

    DDRD |= _BV(PD7) | _BV(PD5) | _BV(PD6);
    DDRF |= _BV(PF0) | _BV(PF1) | _BV(PF2) | _BV(PF3) | _BV(PF6) | _BV(PF7);
    DDRL |= _BV(PL0) | _BV(PL1) | _BV(PL2) | _BV(PL6);
    DDRA |= _BV(PA2) | _BV(PA4) | _BV(PA6);
    DDRC |= _BV(PC1) | _BV(PC3) | _BV(PC7);
    DDRK |= _BV(PK0);
    DDRB |= _BV(PB6);
    DDRJ |= _BV(PJ7);
    DDRE |= _BV(PE7);

    /* SDATO, common open-drain FAULTn and all five STALLn inputs. */
    DDRD &= ~_BV(PD4);
    PORTD |= _BV(PD4);
    DDRE &= ~_BV(PE6);
    PORTE |= _BV(PE6);
    DDRG &= ~(_BV(PG0) | _BV(PG1) | _BV(PG2) | _BV(PG3) | _BV(PG4));
    PORTG |= _BV(PG0) | _BV(PG1) | _BV(PG2) | _BV(PG3) | _BV(PG4);
  }

  /* TI DRV8711 SLVSC40H section 7.4.1: SPI operates in sleep, but RESET
   * does NOT, and UVLO cannot be cleared in sleep. Before waking, positively
   * disable every H-bridge through CTRL.ENBL and read back that exact state.
   * This also protects an MCU-only reset with old driver registers retained.
   * A failed transaction must leave every physical channel asleep. */
  for (driver = 0; driver < RF2000_DRIVER_COUNT; ++driver) {
    uint16_t disabled_control = control[driver] & ~1u;
    write_register(driver, 0, disabled_control);
    if ((read_register(driver, 0) ^ disabled_control) & readback_mask[0])
      readback_failed_mask |= (uint8_t)1 << driver;
  }
  if (readback_failed_mask) {
    fault_reason |= RF2000_DRIVER_FAULT_READBACK;
    rf2000_drivers_disable();
    return;
  }

  /* Temporarily wake ALL five digital/analog sections for a functional
   * shared RESET and STATUS clear. ENBL=0 and STEP=0 prevent motor drive.
   * This is not the public motion-enable path. */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTD |= _BV(PD7);
    PORTF |= _BV(PF2);
    PORTK |= _BV(PK0);
    PORTA |= _BV(PA2);
    PORTC |= _BV(PC7);
  }
  _delay_ms(25);
  PORTE |= _BV(PE7); /* Shared RESET is active HIGH, with every chip awake. */
  _delay_ms(5);
  PORTE &= ~_BV(PE7);
  _delay_ms(5); /* TI requires approximately 1 ms to become functional. */

  /* RESET defaults ENBL=0. Program and verify while all power stages remain
   * disabled. No SPI routine is exposed to real-time execution. */
  for (driver = 0; driver < RF2000_DRIVER_COUNT; ++driver) {
    for (address = 0; address < 8; ++address) {
      uint16_t expected = expected_register(driver, address);
      if (address == 0) expected &= ~1u;
      write_register(driver, address, expected);
    }
  }
  for (driver = 0; driver < RF2000_DRIVER_COUNT; ++driver) {
    for (address = 0; address < 7; ++address) {
      uint16_t expected = expected_register(driver, address);
      if (address == 0) expected &= ~1u;
      uint16_t observed = read_register(driver, address);
      if ((observed ^ expected) & readback_mask[address])
        readback_failed_mask |= (uint8_t)1 << driver;
    }
    initial_status[driver] = read_register(driver, 7);
    /* Exclude STD/STDLAT, which indicate motor stall and are not a power
     * stage fault. The six low bits are OTS/AOCP/BOCP/APDF/BPDF/UVLO. */
    if (initial_status[driver] & 0x003f) fault_reason |= RF2000_DRIVER_FAULT_STATUS;
  }
  if (readback_failed_mask) fault_reason |= RF2000_DRIVER_FAULT_READBACK;
  if (!(PINE & _BV(PE6))) fault_reason |= RF2000_DRIVER_FAULT_PIN;
  /* Sleep first, THEN arm only the configured channels in CTRL. A/B retain
   * ENBL=0 as a second independent barrier in the three-axis test profile. */
  rf2000_drivers_disable();
  if (fault_reason) return;
  /* PE6 is INT6-capable. Latch even a short UVLO pulse between slow steps:
   * FAULTn can recover HIGH before the 1 kHz limit poll samples it. Arm only
   * after the deliberate reset/STATUS-clear phase to avoid stale boot edges.
   * Timer2 subsequently reports the already-latched fault to Grbl. */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    EICRB = (EICRB & ~(_BV(ISC61) | _BV(ISC60))) | _BV(ISC61);
    EIFR = _BV(INTF6); /* Write-one-to-clear only the INT6 pending flag. */
    EIMSK |= _BV(INT6);
  }
  for (driver = 0; driver < RF2000_DRIVER_COUNT; ++driver) {
    write_register(driver, 0, control[driver]);
    if ((read_register(driver, 0) ^ control[driver]) & readback_mask[0])
      readback_failed_mask |= (uint8_t)1 << driver;
  }
  if (readback_failed_mask) fault_reason |= RF2000_DRIVER_FAULT_READBACK;
  if (!(PINE & _BV(PE6))) fault_reason |= RF2000_DRIVER_FAULT_PIN;
  if (!fault_reason) ready = 1;
  else EIMSK &= ~_BV(INT6);
}

static void latch_pin_fault(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    fault_reason |= RF2000_DRIVER_FAULT_PIN;
    ready = 0;
  }
  rf2000_drivers_disable();
}

ISR(INT6_vect)
{
  /* Do not recheck PE6 here: the fault pulse may already have ended. */
  latch_pin_fault();
}

bool rf2000_drivers_fault_active(void)
{
  if (initialized && !(PINE & _BV(PE6))) latch_pin_fault();
  return fault_reason != 0;
}

bool rf2000_drivers_ready(void)
{
  return ready && !fault_reason;
}

void rf2000_drivers_enable(uint8_t axes_mask)
{
  uint8_t waking = 0;
  axes_mask &= RF2000_DRIVER_ACTIVE_MASK;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    /* Recheck inside the same critical section as the GPIO writes. A fault
     * interrupt between the check and the write must never reenable outputs. */
    if (!ready || fault_reason || !(PINE & _BV(PE6))) {
      if (!(PINE & _BV(PE6))) fault_reason |= RF2000_DRIVER_FAULT_PIN;
      ready = 0;
      rf2000_drivers_disable();
    } else {
      waking = axes_mask & ~awake_mask;
      if (axes_mask & 0x01) PORTD |= _BV(PD7); else PORTD &= ~_BV(PD7);
      if (axes_mask & 0x02) PORTF |= _BV(PF2); else PORTF &= ~_BV(PF2);
      if (axes_mask & 0x04) PORTK |= _BV(PK0); else PORTK &= ~_BV(PK0);
      if (axes_mask & 0x08) PORTA |= _BV(PA2); else PORTA &= ~_BV(PA2);
      if (axes_mask & 0x10) PORTC |= _BV(PC7); else PORTC &= ~_BV(PC7);
      awake_mask = axes_mask;
    }
  }
  if (waking) _delay_ms(25); /* Original RF2000v2 STEPPER_ON_DELAY. */
  (void)rf2000_drivers_fault_active();
}

uint8_t rf2000_drivers_fault_reason(void)
{
  return fault_reason;
}

uint8_t rf2000_drivers_readback_failed_mask(void)
{
  return readback_failed_mask;
}

uint16_t rf2000_drivers_initial_status(uint8_t driver)
{
  return driver < RF2000_DRIVER_COUNT ? initial_status[driver] : 0xffff;
}

#endif /* CPU_MAP_2560_RENKFORCE_CTC_V3 */
