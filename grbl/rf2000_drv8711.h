/*
 * RF2000 I851 CTC V3.0 DRV8711 interface.
 * Derived from RF Repetier RF.cpp (GPL-3.0-or-later); part of Grbl.
 * Copyright (c) 2026 RF2000 contributors
 */
#ifndef RF2000_DRV8711_H
#define RF2000_DRV8711_H

#include <stdint.h>
#include <stdbool.h>

#define RF2000_DRIVER_COUNT 5
#define RF2000_DRIVER_FAULT_PIN      0x01u
#define RF2000_DRIVER_FAULT_READBACK 0x02u
#define RF2000_DRIVER_FAULT_STATUS   0x04u

/* Logical Grbl axis bits, not AVR port bits. A/B require an intentional future
 * configuration change as well as N_AXIS >= 5. The test build is XYZ only. */
#ifndef RF2000_DRIVER_ACTIVE_MASK
#define RF2000_DRIVER_ACTIVE_MASK 0x07u
#endif

/* Call once, before enabling motion interrupts. Repeated calls do not clear a
 * latched fault; only an actual MCU reset may reinitialize the hardware. */
void rf2000_drivers_init(void);

/* Main context only: waking a sleeping driver waits the original 25 ms.
 * SPI is never used here. Call before starting the step timer. */
void rf2000_drivers_enable(uint8_t axes_mask);

/* ISR safe, short atomic GPIO operations only; sleeps all FIVE drivers. */
void rf2000_drivers_disable(void);

/* ISR safe. A low common FAULTn immediately sleeps all drivers and latches.
 * Additionally INT6 latches falling edges even if FAULTn has recovered before
 * the next poll. Neither '$X' nor Ctrl-X clears this software latch. */
bool rf2000_drivers_fault_active(void);
bool rf2000_drivers_ready(void);

#if RF2000_ADAPTIVE_MICROSTEPS
/* Foreground only. The caller must stop Timer1, finish Timer0 and keep STEP
 * LOW throughout this barrier. 0..5 selects 1/256..1/8 (stride1..32 in the
 * virtual 1/256 grid). All XYZ CTRL registers are verified before success;
 * no motor wakes and no STEP is generated here. Interrupts remain enabled.
 * A/B and every CTRL field except MODE remain unchanged. */
bool rf2000_drivers_set_microstep_shift(uint8_t shift);

/* Last fully verified common shift; 0xff after an interrupted/failed change.
 * SLEEP and GRBL coordinate resets do not reset the physical driver phase. */
uint8_t rf2000_drivers_microstep_shift(void);
#endif

/* Startup diagnostic snapshots only. No SPI is performed by these getters.
 * driver is zero based (0=X .. 4=B); invalid indices return 0xffff. */
uint8_t rf2000_drivers_fault_reason(void);
uint8_t rf2000_drivers_readback_failed_mask(void);
uint16_t rf2000_drivers_initial_status(uint8_t driver);

#endif
