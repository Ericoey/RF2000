/*
 * RF2000v2 original HD44780 20x4 display transport. Part of Grbl.
 * Copyright (c) 2026 RF2000 contributors
 * GPL-3.0-or-later.
 */
#ifndef RF2000_LCD_H
#define RF2000_LCD_H

#include <stdint.h>

/* Foreground only, once after safe board setup with all motors disabled.
 * Uses the original Repetier 500 ms startup delay with interrupts enabled. */
void rf2000_lcd_init(void);

/* Foreground only. rs=0 sends a command; rs!=0 sends character data.
 * No character execution delay is performed here. The caller must space
 * runtime writes by at least 1 ms (2 ms after CLEAR or RETURN HOME).
 * Interrupts must be enabled on entry; no LCD operation belongs in an ISR. */
void rf2000_lcd_write(uint8_t rs, uint8_t value);

#endif
