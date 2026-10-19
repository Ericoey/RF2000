/*
  limits.h - code pertaining to limit-switches and performing the homing cycle
  Part of Grbl

  Copyright (c) 2017-2026 Gauthier Briere
  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef limits_h
#define limits_h


// Initialize the limits module
void limits_init();

// Returns limit state as a bit-wise uint8 variable.
uint8_t limits_get_state();

#ifdef CPU_MAP_2560_RENKFORCE_CTC_V3
// Fresh physical X-min/Y-min/Z-min/Z-max levels, active bits 0/1/2/3.
uint8_t rf2000_limits_raw_state(void);
// Mandatory start/unlock interlock: physical inputs, stable release, and driver health.
uint8_t limits_motion_allowed(void);
#if RF2000_CNC
// Explicit, bounded withdrawal from exactly one asserted physical limit.
uint8_t rf2000_limits_recover(void);
uint8_t rf2000_limits_recovery_active(void);
#endif
#endif

// Perform one portion of the homing cycle based on the input settings.
void limits_go_home(uint8_t cycle_mask);

// Check for soft limit violations
void limits_soft_check(float *target);

// Hard limit error for RAMPS non interrupt hardware limits
#ifdef ENABLE_RAMPS_HW_LIMITS
  void ramps_hard_limit();
#endif
#endif
