/*
 * RF2000v2 original HD44780 20x4 display transport. Part of Grbl.
 * Derived from RF Repetier firmware (GPL-3.0-or-later):
 *   references/repetier/Repetier/pins.h:740-757 (RF2000v2 wiring)
 *   references/repetier/Repetier/ui.cpp:304-414,438-500 (4-bit protocol/init)
 *   references/repetier/Repetier/ui.h:257-265 (320 us character delay)
 * Copyright (c) 2026 RF2000 contributors
 *
 * The original printer menus are deliberately not imported. GPIO writes use
 * short atomic masked updates because STEP, nSLEEP and driver CS share these
 * ports. Startup delays and E pulse delays leave interrupts enabled. Runtime
 * character spacing is owned by the foreground panel controller.
 *
 * PA0/D_LED backlight activation is an addition based on schematic sheet 2;
 * the supplied Repetier code does not explicitly configure that signal.
 */
#include "grbl.h"
#include "rf2000_lcd.h"
#include <util/atomic.h>

#if RF2000_LOCAL_PANEL

#ifndef CPU_MAP_2560_RENKFORCE_CTC_V3
#error "RF2000 local display requires the RF2000 CTC V3 CPU map"
#endif

#define LCD_PORTK_MASK (_BV(PK1) | _BV(PK2) | _BV(PK3))
#define LCD_BACKLIGHT_MASK _BV(PA0)

static void lcd_nibble(uint8_t value)
{
  /* E is LOW while changing data. Preserve the independent GPIO owners,
   * particularly PORTF STEP bits and PORTK PK0 (Z nSLEEP). */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTF = (PORTF & (uint8_t)~_BV(PF5)) | ((value & 0x01u) ? _BV(PF5) : 0);
    PORTK = (PORTK & (uint8_t)~_BV(PK2)) | ((value & 0x02u) ? _BV(PK2) : 0);
    PORTA = (PORTA & (uint8_t)~_BV(PA1)) | ((value & 0x04u) ? _BV(PA1) : 0);
    PORTJ = (PORTJ & (uint8_t)~_BV(PJ3)) | ((value & 0x08u) ? _BV(PJ3) : 0);
  }
  _delay_us(1);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { PORTK |= _BV(PK3); }
  /* An interrupt may extend E HIGH, but cannot shorten its >=1 us pulse. */
  _delay_us(1);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { PORTK &= (uint8_t)~_BV(PK3); }
  _delay_us(1);
}

void rf2000_lcd_write(uint8_t rs, uint8_t value)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTK = (PORTK & (uint8_t)~_BV(PK1)) | (rs ? _BV(PK1) : 0);
  }
  lcd_nibble(value >> 4);
  lcd_nibble(value & 0x0fu);
}

static void lcd_startup_command(uint8_t value)
{
  /* Retain the original conservative command spacing during initialization.
   * Runtime rendering schedules bytes instead of busy-waiting for 320 us. */
  _delay_us(320);
  rf2000_lcd_write(0, value);
}

void rf2000_lcd_init(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    /* E and all data/RS latches LOW before configuring output drivers.
     * R/W is physically grounded, so no busy-read or direction reversal. */
    PORTK &= (uint8_t)~LCD_PORTK_MASK;
    PORTF &= (uint8_t)~_BV(PF5);
    PORTA &= (uint8_t)~(_BV(PA1) | LCD_BACKLIGHT_MASK);
    PORTJ &= (uint8_t)~_BV(PJ3);
    DDRK &= (uint8_t)~LCD_PORTK_MASK;
    DDRF &= (uint8_t)~_BV(PF5);
    DDRA &= (uint8_t)~(_BV(PA1) | LCD_BACKLIGHT_MASK);
    DDRJ &= (uint8_t)~_BV(PJ3);
  }
  _delay_ms(500);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    DDRK |= LCD_PORTK_MASK;
    DDRF |= _BV(PF5);
    DDRA |= _BV(PA1);
    DDRJ |= _BV(PJ3);
    /* Q8 is a low-side backlight switch; a HIGH D_LED turns it on. */
    PORTA |= LCD_BACKLIGHT_MASK;
    DDRA |= LCD_BACKLIGHT_MASK;
  }
  _delay_us(10);
  lcd_nibble(0x03);
  _delay_ms(5);
  lcd_nibble(0x03);
  _delay_us(150);
  lcd_nibble(0x03);
  _delay_us(150);
  lcd_nibble(0x02);
  _delay_us(150);
  lcd_startup_command(0x28); /* 4-bit, two controller lines, 5x7 font. */
  lcd_startup_command(0x01); /* Clear DDRAM. */
  _delay_ms(2);
  lcd_startup_command(0x06); /* Increment cursor, no display shift. */
  lcd_startup_command(0x0c); /* Display on, cursor and blink off. */
}

#endif
