/*
  limits.c - code pertaining to limit-switches and performing the homing cycle
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

#include "grbl.h"

// Homing axis search distance multiplier. Computed by this value times the cycle travel.
#ifndef HOMING_AXIS_SEARCH_SCALAR
  #define HOMING_AXIS_SEARCH_SCALAR  1.5 // Must be > 1 to ensure limit switch will be engaged.
#endif
#ifndef HOMING_AXIS_LOCATE_SCALAR
  #define HOMING_AXIS_LOCATE_SCALAR  5.0 // Must be > 1 to ensure limit switch is cleared.
#endif

#ifdef CPU_MAP_2560_RENKFORCE_CTC_V3
#include "rf2000_drv8711.h"

// Physical switch bits: X-min, Y-min, Z-min, Z-max. Repetier's
// READ(pin) != ENDSTOP_*_INVERTING (true) means active LOW.
#define RF2000_LIMIT_RELEASE_TICKS 5
#define RF2000_LIMIT_X_MIN bit(0)
#define RF2000_LIMIT_Y_MIN bit(1)
#define RF2000_LIMIT_Z_MIN bit(2)
#define RF2000_LIMIT_Z_MAX bit(3)

static volatile uint8_t rf2000_limit_held;
static volatile uint8_t rf2000_limit_release_ticks;
// Only selected, verified homing switches may be ignored by the hard-limit
// monitor. The opposite Z switch remains a hard limit throughout homing.
static volatile uint8_t rf2000_homing_expected;
#if RF2000_CNC
static volatile uint8_t rf2000_recovering;

uint8_t rf2000_limits_recovery_active(void)
{
  return rf2000_recovering;
}
#endif

uint8_t rf2000_limits_raw_state(void)
{
  const uint8_t pc = PINC;
  const uint8_t ph = PINH;
  uint8_t state = 0;
  if (!(pc & bit(PC2))) { state |= RF2000_LIMIT_X_MIN; }
  if (!(pc & bit(PC0))) { state |= RF2000_LIMIT_Y_MIN; }
  if (!(pc & bit(PC6))) { state |= RF2000_LIMIT_Z_MIN; }
  if (!(ph & bit(PH1))) { state |= RF2000_LIMIT_Z_MAX; }
  return state;
}

static uint8_t rf2000_limits_axis_state(uint8_t physical)
{
  uint8_t state = physical & (RF2000_LIMIT_X_MIN | RF2000_LIMIT_Y_MIN);
  if (physical & (RF2000_LIMIT_Z_MIN | RF2000_LIMIT_Z_MAX)) { state |= bit(AXIS_3); }
  return state;
}

uint8_t limits_motion_allowed(void)
{
  #if RF2000_LOCAL_PANEL
    if (rf2000_panel_stop_active()) return false;
  #endif
  // Read physical levels again at every admission point. A low pulse between
  // poll ticks must also prevent a new movement. Runtime $5 cannot invert this.
  uint8_t active = rf2000_limits_raw_state() | rf2000_limit_held;
  if (sys.state == STATE_HOMING) { active &= ~rf2000_homing_expected; }
  return !active && !rf2000_drivers_fault_active() && rf2000_drivers_ready();
}

void limits_init(void)
{
  const uint8_t saved_sreg = SREG;
  cli();
  DDRC &= ~(bit(PC2) | bit(PC0) | bit(PC6));
  DDRH &= ~bit(PH1);
  // Retain Repetier's pull-ups, including the optical Z-min input. Never
  // configure the absent X/Y-max pins or inherit RAMPS assignments.
  PORTC |= bit(PC2) | bit(PC0) | bit(PC6);
  PORTH |= bit(PH1);
  rf2000_limit_held = rf2000_limits_raw_state();
  rf2000_limit_release_ticks = 0;
  rf2000_homing_expected = 0;
  #if RF2000_CNC
    rf2000_recovering = 0;
  #endif
  // Timer2 CTC: 16 MHz / 64 / 250 = 1 kHz. OC2A/B remain disconnected;
  // this timer cannot also be used for a spindle, heater, fan or analog PWM.
  TIMSK2 = 0;
  TCCR2A = bit(WGM21);
  TCCR2B = 0;
  TCNT2 = 0;
  OCR2A = (F_CPU / 64UL / 1000UL) - 1;
  TIFR2 = bit(OCF2A) | bit(OCF2B) | bit(TOV2);
  TCCR2B = bit(CS22);
  TIMSK2 = bit(OCIE2A);
  SREG = saved_sreg;
}

uint8_t limits_get_state(void)
{
  uint8_t physical = rf2000_limits_raw_state();
  if (sys.state == STATE_HOMING && rf2000_homing_expected) {
    // The homing axis lock must see the designated reference switch, rather
    // than treating Z-min and Z-max as interchangeable references.
    physical &= rf2000_homing_expected;
  }
  return rf2000_limits_axis_state(physical);
}

ISR(TIMER2_COMPA_vect)
{
  const uint8_t raw = rf2000_limits_raw_state();
  if (raw) {
    rf2000_limit_held |= raw; // Assert immediately; never debounce a stop.
    rf2000_limit_release_ticks = 0;
  } else if (rf2000_limit_release_ticks < RF2000_LIMIT_RELEASE_TICKS) {
    if (++rf2000_limit_release_ticks == RF2000_LIMIT_RELEASE_TICKS) {
      rf2000_limit_held = 0;
    }
  }
  #if RF2000_CNC
    // After five fully free samples the withdrawn switch is a hard limit
    // again, including during the remainder of the bounded recovery move.
    if (rf2000_recovering && !rf2000_limit_held) rf2000_homing_expected = 0;
  #endif
  uint8_t active = raw | rf2000_limit_held;
  if (sys.state == STATE_HOMING) { active &= ~rf2000_homing_expected; }
  const uint8_t fault = rf2000_drivers_fault_active() || !rf2000_drivers_ready();
  if (active || fault) {
    // GPIO-only, ISR-safe, and includes both reserve drivers. No SPI here.
    rf2000_drivers_disable();
    // Already alarmed machines remain disabled without repeatedly resetting
    // the protocol. $X and every motion start independently recheck inputs.
    if (sys.state != STATE_ALARM && !sys_rt_exec_alarm) {
      mc_reset();
      system_set_exec_alarm(fault ? EXEC_ALARM_DRIVER_FAULT : EXEC_ALARM_HARD_LIMIT);
    }
  }
  #if RF2000_LOCAL_PANEL
    rf2000_panel_tick_isr();
  #endif
}
#else
void limits_init()
{
  // Set as input pins
  MIN_LIMIT_DDR(0) &= ~(1<<MIN_LIMIT_BIT(0));
  MIN_LIMIT_DDR(1) &= ~(1<<MIN_LIMIT_BIT(1));
  MIN_LIMIT_DDR(2) &= ~(1<<MIN_LIMIT_BIT(2));
  #if N_AXIS > 3
  MIN_LIMIT_DDR(3) &= ~(1<<MIN_LIMIT_BIT(3));
  #endif
  #if N_AXIS > 4
  MIN_LIMIT_DDR(4) &= ~(1<<MIN_LIMIT_BIT(4));
  #endif
  #if N_AXIS > 5
  MIN_LIMIT_DDR(5) &= ~(1<<MIN_LIMIT_BIT(5));
  #endif
  MAX_LIMIT_DDR(0) &= ~(1<<MAX_LIMIT_BIT(0));
  MAX_LIMIT_DDR(1) &= ~(1<<MAX_LIMIT_BIT(1));
  MAX_LIMIT_DDR(2) &= ~(1<<MAX_LIMIT_BIT(2));
  #if N_AXIS > 3
  MAX_LIMIT_DDR(3) &= ~(1<<MAX_LIMIT_BIT(3));
  #endif
  #if N_AXIS > 4
  MAX_LIMIT_DDR(4) &= ~(1<<MAX_LIMIT_BIT(4));
  #endif
  #if N_AXIS > 5
  MAX_LIMIT_DDR(5) &= ~(1<<MAX_LIMIT_BIT(5));
  #endif

  #ifdef DISABLE_LIMIT_PIN_PULL_UP
    MIN_LIMIT_PORT(0) &= ~(1<<MIN_LIMIT_BIT(0)); // Normal low operation. Requires external pull-down.
    MIN_LIMIT_PORT(1) &= ~(1<<MIN_LIMIT_BIT(1)); // Normal low operation. Requires external pull-down.
    MIN_LIMIT_PORT(2) &= ~(1<<MIN_LIMIT_BIT(2)); // Normal low operation. Requires external pull-down.
    #if N_AXIS > 3
      MIN_LIMIT_PORT(3) &= ~(1<<MIN_LIMIT_BIT(3)); // Normal low operation. Requires external pull-down.
    #endif
    #if N_AXIS > 4
      MIN_LIMIT_PORT(4) &= ~(1<<MIN_LIMIT_BIT(4)); // Normal low operation. Requires external pull-down.
    #endif
    #if N_AXIS > 5
      MIN_LIMIT_PORT(5) &= ~(1<<MIN_LIMIT_BIT(5)); // Normal low operation. Requires external pull-down.
    #endif
    MAX_LIMIT_PORT(0) &= ~(1<<MAX_LIMIT_BIT(0)); // Normal low operation. Requires external pull-down.
    MAX_LIMIT_PORT(1) &= ~(1<<MAX_LIMIT_BIT(1)); // Normal low operation. Requires external pull-down.
    MAX_LIMIT_PORT(2) &= ~(1<<MAX_LIMIT_BIT(2)); // Normal low operation. Requires external pull-down.
    #if N_AXIS > 3
      MAX_LIMIT_PORT(3) &= ~(1<<MAX_LIMIT_BIT(3)); // Normal low operation. Requires external pull-down.
    #endif
    #if N_AXIS > 4
      MAX_LIMIT_PORT(4) &= ~(1<<MAX_LIMIT_BIT(4)); // Normal low operation. Requires external pull-down.
    #endif
    #if N_AXIS > 5
      MAX_LIMIT_PORT(5) &= ~(1<<MAX_LIMIT_BIT(5)); // Normal low operation. Requires external pull-down.
    #endif
  #else
    MIN_LIMIT_PORT(0) |= (1<<MIN_LIMIT_BIT(0));  // Enable internal pull-up resistors. Normal high operation.
    MIN_LIMIT_PORT(1) |= (1<<MIN_LIMIT_BIT(1));  // Enable internal pull-up resistors. Normal high operation.
    MIN_LIMIT_PORT(2) |= (1<<MIN_LIMIT_BIT(2));  // Enable internal pull-up resistors. Normal high operation.
    #if N_AXIS > 3
      MIN_LIMIT_PORT(3) |= (1<<MIN_LIMIT_BIT(3));  // Enable internal pull-up resistors. Normal high operation.
    #endif
    #if N_AXIS > 4
      MIN_LIMIT_PORT(4) |= (1<<MIN_LIMIT_BIT(4));  // Enable internal pull-up resistors. Normal high operation.
    #endif
    #if N_AXIS > 5
      MIN_LIMIT_PORT(5) |= (1<<MIN_LIMIT_BIT(5));  // Enable internal pull-up resistors. Normal high operation.
    #endif
    MAX_LIMIT_PORT(0) |= (1<<MAX_LIMIT_BIT(0));  // Enable internal pull-up resistors. Normal high operation.
    MAX_LIMIT_PORT(1) |= (1<<MAX_LIMIT_BIT(1));  // Enable internal pull-up resistors. Normal high operation.
    MAX_LIMIT_PORT(2) |= (1<<MAX_LIMIT_BIT(2));  // Enable internal pull-up resistors. Normal high operation.
    #if N_AXIS > 3
      MAX_LIMIT_PORT(3) |= (1<<MAX_LIMIT_BIT(3));  // Enable internal pull-up resistors. Normal high operation.
    #endif
    #if N_AXIS > 4
      MAX_LIMIT_PORT(4) |= (1<<MAX_LIMIT_BIT(4));  // Enable internal pull-up resistors. Normal high operation.
    #endif
    #if N_AXIS > 5
      MAX_LIMIT_PORT(5) |= (1<<MAX_LIMIT_BIT(5));  // Enable internal pull-up resistors. Normal high operation.
    #endif
  #endif
}

#if N_AXIS == 4
  static volatile uint8_t * const max_limit_pins[N_AXIS] = {&MAX_LIMIT_PIN(0), &MAX_LIMIT_PIN(1), &MAX_LIMIT_PIN(2), &MAX_LIMIT_PIN(3)};
  static volatile uint8_t * const min_limit_pins[N_AXIS] = {&MIN_LIMIT_PIN(0), &MIN_LIMIT_PIN(1), &MIN_LIMIT_PIN(2), &MIN_LIMIT_PIN(3)};
  static const uint8_t max_limit_bits[N_AXIS] = {MAX_LIMIT_BIT(0), MAX_LIMIT_BIT(1), MAX_LIMIT_BIT(2), MAX_LIMIT_BIT(3)};
  static const uint8_t min_limit_bits[N_AXIS] = {MIN_LIMIT_BIT(0), MIN_LIMIT_BIT(1), MIN_LIMIT_BIT(2), MIN_LIMIT_BIT(3)};
#elif N_AXIS == 5
  static volatile uint8_t * const max_limit_pins[N_AXIS] = {&MAX_LIMIT_PIN(0), &MAX_LIMIT_PIN(1), &MAX_LIMIT_PIN(2), &MAX_LIMIT_PIN(3), &MAX_LIMIT_PIN(4)};
  static volatile uint8_t * const min_limit_pins[N_AXIS] = {&MIN_LIMIT_PIN(0), &MIN_LIMIT_PIN(1), &MIN_LIMIT_PIN(2), &MIN_LIMIT_PIN(3), &MIN_LIMIT_PIN(4)};
  static const uint8_t max_limit_bits[N_AXIS] = {MAX_LIMIT_BIT(0), MAX_LIMIT_BIT(1), MAX_LIMIT_BIT(2), MAX_LIMIT_BIT(3), MAX_LIMIT_BIT(4)};
  static const uint8_t min_limit_bits[N_AXIS] = {MIN_LIMIT_BIT(0), MIN_LIMIT_BIT(1), MIN_LIMIT_BIT(2), MIN_LIMIT_BIT(3), MIN_LIMIT_BIT(4)};
#elif N_AXIS == 6
  static volatile uint8_t * const max_limit_pins[N_AXIS] = {&MAX_LIMIT_PIN(0), &MAX_LIMIT_PIN(1), &MAX_LIMIT_PIN(2), &MAX_LIMIT_PIN(3), &MAX_LIMIT_PIN(4), &MAX_LIMIT_PIN(5)};
  static volatile uint8_t * const min_limit_pins[N_AXIS] = {&MIN_LIMIT_PIN(0), &MIN_LIMIT_PIN(1), &MIN_LIMIT_PIN(2), &MIN_LIMIT_PIN(3), &MIN_LIMIT_PIN(4), &MIN_LIMIT_PIN(5)};
  static const uint8_t max_limit_bits[N_AXIS] = {MAX_LIMIT_BIT(0), MAX_LIMIT_BIT(1), MAX_LIMIT_BIT(2), MAX_LIMIT_BIT(3), MAX_LIMIT_BIT(4), MAX_LIMIT_BIT(5)};
  static const uint8_t min_limit_bits[N_AXIS] = {MIN_LIMIT_BIT(0), MIN_LIMIT_BIT(1), MIN_LIMIT_BIT(2), MIN_LIMIT_BIT(3), MIN_LIMIT_BIT(4), MIN_LIMIT_BIT(5)};
#else
  static volatile uint8_t * const max_limit_pins[N_AXIS] = {&MAX_LIMIT_PIN(0), &MAX_LIMIT_PIN(1), &MAX_LIMIT_PIN(2)};
  static volatile uint8_t * const min_limit_pins[N_AXIS] = {&MIN_LIMIT_PIN(0), &MIN_LIMIT_PIN(1), &MIN_LIMIT_PIN(2)};
  static const uint8_t max_limit_bits[N_AXIS] = {MAX_LIMIT_BIT(0), MAX_LIMIT_BIT(1), MAX_LIMIT_BIT(2)};
  static const uint8_t min_limit_bits[N_AXIS] = {MIN_LIMIT_BIT(0), MIN_LIMIT_BIT(1), MIN_LIMIT_BIT(2)};
#endif

// Returns limit state as a bit-wise uint8 variable. Each bit indicates an axis limit, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. AXIS_3 is (1<<2) or bit 2, and AXIS_2 is (1<<1) or bit 1.
uint8_t limits_get_state()
{
  uint8_t limit_state_max = 0;
  uint8_t limit_state_min = 0;
  uint8_t pin;
  uint8_t idx;
  uint8_t unused_bits = 0xff - (1<<N_AXIS) + 1;
  #ifdef INVERT_LIMIT_PIN_MASK
    #error "INVERT_LIMIT_PIN_MASK is not implemented, use INVERT_<MAX|MIN>_LIMIT_PIN_MASK"
  #endif
  for (idx=0; idx<N_AXIS; idx++) {
    pin = *max_limit_pins[idx] & (1<<max_limit_bits[idx]);
    pin = !pin;
    #ifdef INVERT_MAX_LIMIT_PIN_MASK
      if (bit_istrue(INVERT_MAX_LIMIT_PIN_MASK, bit(idx))) { pin = !pin; }
    #endif
    if (pin) {
      limit_state_max |= (1 << idx);
    }
    pin = *min_limit_pins[idx] & (1<<min_limit_bits[idx]);
    pin = !pin;
    #ifdef INVERT_MIN_LIMIT_PIN_MASK
      if (bit_istrue(INVERT_MIN_LIMIT_PIN_MASK, bit(idx))) { pin = !pin; }
    #endif
    if (pin) {
      limit_state_min |= (1 << idx);
    }
  }
  if (bit_istrue(settings.flags,BITFLAG_INVERT_LIMIT_PINS)) {
    return(~((limit_state_max & limit_state_min) | unused_bits));
  } else {
    return(limit_state_max | limit_state_min);
  }
}

#endif // RF2000 board / generic limit inputs

#ifdef ENABLE_RAMPS_HW_LIMITS
  void ramps_hard_limit()
  {
    if (bit_istrue(settings.flags,BITFLAG_HARD_LIMIT_ENABLE)) {
      // Ignore limit switches if already in an alarm state or in-process of executing an alarm.
      // When in the alarm state, Grbl should have been reset or will force a reset, so any pending
      // moves in the planner and serial buffers are all cleared and newly sent blocks will be
      // locked out until a homing cycle or a kill lock command. Allows the user to disable the hard
      // limit setting if their limits are constantly triggering after a reset and move their axes.
      if ((sys.state != STATE_ALARM) && (sys.state != STATE_HOMING)) {
        if (!(sys_rt_exec_alarm)) {
          #ifdef HARD_LIMIT_FORCE_STATE_CHECK
            // Check limit pin state.
            if (limits_get_state()) {
              mc_reset(); // Initiate system kill.
              system_set_exec_alarm(EXEC_ALARM_HARD_LIMIT); // Indicate hard limit critical event
            }
          #else
            mc_reset(); // Initiate system kill.
            system_set_exec_alarm(EXEC_ALARM_HARD_LIMIT); // Indicate hard limit critical event
          #endif
        }
      }
    }
  }
#endif

static uint8_t axislock_active(uint8_t *axislock)
{
  uint8_t res = 0;
  uint8_t idx;

  for (idx = 0; idx < N_AXIS; idx++)
    if (axislock[idx]) {
      res = 1;
      break;
    }

  return res;
}


// Homes the specified cycle axes, sets the machine position, and performs a pull-off motion after
// completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
// the trigger point of the limit switches. The rapid stops are handled by a system level axis lock
// mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically
// circumvent the processes for executing motions in normal operation.
// NOTE: Only the abort realtime command can interrupt this process.
// TODO: Move limit pin-specific calls to a general function for portability.
void limits_go_home(uint8_t cycle_mask)
{
  #if defined(CPU_MAP_2560_RENKFORCE_CTC_V3) && !RF2000_HOMING_VERIFIED && !RF2000_Y_HOMING_TEST && !RF2000_CNC
    (void)cycle_mask;
    system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
    return;
  #endif

  if (sys.abort) { return; } // Block if system reset has been issued.
  #if RF2000_Y_HOMING_TEST
    if (cycle_mask != bit(AXIS_2)) {
      system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_APPROACH);
      return;
    }
  #endif
  #ifdef CPU_MAP_2560_RENKFORCE_CTC_V3
    // X/Y have MIN switches only. Z is selected from its physical MIN/MAX.
    // A mismatched future X/Y homing direction is a configuration fault.
    if ((cycle_mask & (bit(AXIS_1) | bit(AXIS_2))) & ~settings.homing_dir_mask) {
      system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_APPROACH);
      return;
    }
    rf2000_homing_expected = cycle_mask & (bit(AXIS_1) | bit(AXIS_2));
    if (cycle_mask & bit(AXIS_3)) {
      rf2000_homing_expected |= (settings.homing_dir_mask & bit(AXIS_3)) ?
        RF2000_LIMIT_Z_MIN : RF2000_LIMIT_Z_MAX;
    }
    rf2000_panel_homing_axes(cycle_mask);
  #endif

  // Initialize plan data struct for homing motion. Spindle and coolant are disabled.
  plan_line_data_t plan_data;
  plan_line_data_t *pl_data = &plan_data;
  memset(pl_data,0,sizeof(plan_line_data_t));
  pl_data->condition = (PL_COND_FLAG_SYSTEM_MOTION|PL_COND_FLAG_NO_FEED_OVERRIDE);
  pl_data->line_number = HOMING_CYCLE_LINE_NUMBER;

  // Initialize variables used for homing computations.
  uint8_t n_cycle = (2*N_HOMING_LOCATE_CYCLE+1);
  uint8_t step_pin[N_AXIS];
  float target[N_AXIS];
  float max_travel = 0.0;
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {
    // Initialize step pin masks
    step_pin[idx] = get_step_pin_mask(idx);
    #ifdef COREXY
      if ((idx==A_MOTOR)||(idx==B_MOTOR)) { step_pin[idx] = (get_step_pin_mask(AXIS_1)|get_step_pin_mask(AXIS_2)); }
    #endif

    if (bit_istrue(cycle_mask,bit(idx))) {
      // Set target based on max_travel setting. Ensure homing switches engaged with search scalar.
      // NOTE: settings.max_travel[] is stored as a negative value.
      max_travel = max(max_travel,(-HOMING_AXIS_SEARCH_SCALAR)*settings.max_travel[idx]);
      // Special case of rotary axis with max_travel = 0
      if ((idx >= N_AXIS_LINEAR) && (max_travel == 0)) { max_travel = HOMING_AXIS_SEARCH_SCALAR * 360.0; }
      if (max_travel < HOMING_AXIS_LOCATE_SCALAR) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_TRAVEL); }
    }
  }

  // Set search mode with approach at seek rate to quickly engage the specified cycle_mask limit switches.
  #if RF2000_Y_HOMING_TEST
    // Do not use the machine envelope (normally 290*1.5 = 435 mm) on a bench.
    max_travel = RF2000_Y_HOMING_SEARCH_CHUNK_MM;
  #endif
  bool approach = true;
  float homing_rate = settings.homing_seek_rate;
  uint8_t limit_state, n_active_axis;
  uint8_t axislock[N_AXIS];
  #if RF2000_Y_HOMING_TEST
    uint8_t initial_search = true;
  #endif
  do {
    #if RF2000_Y_HOMING_TEST
      retry_initial_search: ;
      uint8_t repeat_initial_search = false;
    #endif
    system_convert_array_steps_to_mpos(target,sys_position);

    // Initialize and declare variables needed for homing routine.
    n_active_axis = 0;
    for (idx=0; idx<N_AXIS; idx++) {
      axislock[idx]=0;
      // Set target location for active axes and setup computation for homing rate.
      if (bit_istrue(cycle_mask,bit(idx))) {
        n_active_axis++;
        #ifdef COREXY
          if (idx == AXIS_1) {
            int32_t axis_position = system_convert_corexy_to_y_axis_steps(sys_position);
            sys_position[A_MOTOR] = axis_position;
            sys_position[B_MOTOR] = -axis_position;
          } else if (idx == AXIS_2) {
            int32_t axis_position = system_convert_corexy_to_x_axis_steps(sys_position);
            sys_position[A_MOTOR] = sys_position[B_MOTOR] = axis_position;
          } else {
            sys_position[AXIS_3] = 0;
          }
        #else
          sys_position[idx] = 0;
        #endif
        // Set target direction based on cycle mask and homing cycle approach state.
        // NOTE: This happens to compile smaller than any other implementation tried.
        if (bit_istrue(settings.homing_dir_mask,bit(idx))) {
          if (approach) { target[idx] = -max_travel; }
          else { target[idx] = max_travel; }
        } else {
          if (approach) { target[idx] = max_travel; }
          else { target[idx] = -max_travel; }
        }
        // Apply axislock to the step port pins active in this cycle.
        axislock[idx] = step_pin[idx];
        sys.homing_axis_lock[idx] = axislock[idx];
      }

    }
    homing_rate *= sqrt(n_active_axis); // [sqrt(N_AXIS)] Adjust so individual axes all move at homing rate.

    // Perform homing cycle. Planner buffer should be empty, as required to initiate the homing cycle.
    pl_data->feed_rate = homing_rate; // Set current homing rate.
    plan_buffer_line(target, pl_data); // Bypass mc_line(). Directly plan homing motion.

    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute homing motion and clear existing flags.
    #if RF2000_LOCAL_PANEL
      #if RF2000_Y_HOMING_TEST
        rf2000_panel_homing_phase(initial_search ? 1 : (approach ? 3 : (n_cycle == 2 ? 2 : 4)));
      #else
        rf2000_panel_homing_phase(approach ? (n_cycle == 2*N_HOMING_LOCATE_CYCLE+1 ? 1 : 3) :
          (n_cycle == 2*N_HOMING_LOCATE_CYCLE ? 2 : 4));
      #endif
    #endif
    st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
    st_wake_up(); // Initiate motion
    do {
      if (approach) {
        // Check limit state. Lock out cycle axes when they change.
        limit_state = limits_get_state();
        for (idx=0; idx<N_AXIS; idx++) {
          if (axislock[idx] & step_pin[idx]) {
            if (limit_state & (1 << idx)) {
              #ifdef COREXY
                if (idx==AXIS_3) { axislock[idx] &= ~(step_pin[AXIS_3]); }
                #if N_AXIS > 3
                  else if (idx==AXIS_4) { axislock[idx] &= ~(step_pin[AXIS_4]); }
                #endif
                #if N_AXIS > 4
                  else if (idx==AXIS_5) { axislock[idx] &= ~(step_pin[AXIS_5]); }
                #endif
                #if N_AXIS > 5
                  else if (idx==AXIS_6) { axislock[idx] &= ~(step_pin[AXIS_6]); }
                #endif
                else { axislock[idx] &= ~(step_pin[A_MOTOR]|step_pin[B_MOTOR]); }
              #else
                axislock[idx] &= ~(step_pin[idx]);
              #endif
            }
          }
          sys.homing_axis_lock[idx] = axislock[idx];
        }
      }

      st_prep_buffer(); // Check and prep segment buffer. NOTE: Should take no longer than 200us.
      rf2000_panel_service(); // Foreground only; keys/STOP continue in Timer2.

      // Exit routines: No time to run protocol_execute_realtime() in this loop.
      if (sys_rt_exec_state & (EXEC_SAFETY_DOOR | EXEC_RESET | EXEC_CYCLE_STOP)) {
        uint8_t rt_exec = sys_rt_exec_state;
        // Homing failure condition: Reset issued during cycle.
        // Preserve a driver/hard-limit alarm already raised by an ISR.
        if (!sys_rt_exec_alarm) {
          if (rt_exec & EXEC_RESET) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET); }
          // Homing failure condition: Safety door was opened.
          else if (rt_exec & EXEC_SAFETY_DOOR) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_DOOR); }
          // Limit switch still engaged after pull-off motion.
          else if (!approach && (limits_get_state() & cycle_mask)) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_PULLOFF); }
          // Limit switch not found during approach.
          else if (approach && (rt_exec & EXEC_CYCLE_STOP)) {
            #if RF2000_Y_HOMING_TEST
              if (initial_search) {
                // User-requested endless bench rotation: reaching the end
                // of a planner block is not a failed reference search.
                repeat_initial_search = !(limits_get_state() & cycle_mask);
              } else
            #endif
            { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_APPROACH); }
          }
        }
        if (sys_rt_exec_alarm) {
          mc_reset(); // Stop motors, if they are running.
          #ifdef CPU_MAP_2560_RENKFORCE_CTC_V3
            rf2000_homing_expected = 0;
          #endif
          protocol_execute_realtime();
          return;
        } else {
          // Pull-off motion complete. Disable CYCLE_STOP from executing.
          system_clear_exec_state_flag(EXEC_CYCLE_STOP);
          break;
        }
      }

    } while (axislock_active(axislock));
    st_reset(); // Immediately force kill steppers and reset step segment buffer.
    #if RF2000_Y_HOMING_TEST
      // A contact can coincide with the final STEP of a planner block.
      // With timers stopped, discard only that stale completion flag so it
      // cannot prematurely finish the following pull-off. Keep RESET/faults.
      system_clear_exec_state_flag(EXEC_CYCLE_STOP);
      if (repeat_initial_search) { goto retry_initial_search; }
      initial_search = false;
    #endif
    #if RF2000_LOCAL_PANEL
      for (uint16_t wait_ms = 0; wait_ms < settings.homing_debounce_delay; ++wait_ms) {
        _delay_ms(1);
        rf2000_panel_service();
        if (sys_rt_exec_state & EXEC_RESET) break;
      }
    #else
      delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.
    #endif

    // Reverse direction and reset homing rate for locate cycle(s).
    approach = !approach;

    // After first cycle, homing enters locating phase. Shorten search to pull-off distance.
    if (approach) {
      max_travel = settings.homing_pulloff*HOMING_AXIS_LOCATE_SCALAR;
      #if RF2000_Y_HOMING_TEST
        max_travel = min(max_travel, RF2000_Y_HOMING_LOCATE_MAX_MM);
      #endif
      homing_rate = settings.homing_feed_rate;
    } else {
      max_travel = settings.homing_pulloff;
      homing_rate = settings.homing_seek_rate;
    }
  } while (n_cycle-- > 0);

  // The active cycle axes should now be homed and machine limits have been located. By
  // default, Grbl defines machine space as all negative, as do most CNCs. Since limit switches
  // can be on either side of an axes, check and set axes machine zero appropriately. Also,
  // set up pull-off maneuver from axes limit switches that have been homed. This provides
  // some initial clearance off the switches and should also help prevent them from falsely
  // triggering when hard limits are enabled or when more than one axes shares a limit pin.
  int32_t set_axis_position;
  // Set machine positions for homed limit switches. Don't update non-homed axes.
  for (idx=0; idx<N_AXIS; idx++) {
    // NOTE: settings.max_travel[] is stored as a negative value.
    if (cycle_mask & bit(idx)) {
      #ifdef HOMING_FORCE_SET_ORIGIN
        set_axis_position = 0;
      #else
        if ( bit_istrue(settings.homing_dir_mask,bit(idx)) ) {
          set_axis_position = lround((settings.max_travel[idx]+settings.homing_pulloff)*settings.steps_per_mm[idx]);
        } else {
          set_axis_position = lround(-settings.homing_pulloff*settings.steps_per_mm[idx]);
        }
      #endif

      #ifdef COREXY
        if (idx==AXIS_1) {
          int32_t off_axis_position = system_convert_corexy_to_y_axis_steps(sys_position);
          sys_position[A_MOTOR] = set_axis_position + off_axis_position;
          sys_position[B_MOTOR] = set_axis_position - off_axis_position;
        } else if (idx==AXIS_2) {
          int32_t off_axis_position = system_convert_corexy_to_x_axis_steps(sys_position);
          sys_position[A_MOTOR] = off_axis_position + set_axis_position;
          sys_position[B_MOTOR] = off_axis_position - set_axis_position;
        } else {
          sys_position[idx] = set_axis_position;
        }
      #else
        sys_position[idx] = set_axis_position;
      #endif

    }
  }
  sys.step_control = STEP_CONTROL_NORMAL_OP; // Return step control to normal operation.
  #ifdef CPU_MAP_2560_RENKFORCE_CTC_V3
    rf2000_homing_expected = 0;
  #endif
}


// Performs a soft limit check. Called from mc_line() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limits_soft_check(float *target)
{
  if (system_check_travel_limits(target)) {
    sys.soft_limit = true;
    // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within
    // workspace volume so just come to a controlled stop so position is not lost. When complete
    // enter alarm mode.
    if (sys.state == STATE_CYCLE) {
      system_set_exec_state_flag(EXEC_FEED_HOLD);
      do {
        protocol_execute_realtime();
        if (sys.abort) { return; }
      } while ( sys.state != STATE_IDLE );
    }
    mc_reset(); // Issue system reset and ensure spindle and coolant are shutdown.
    system_set_exec_alarm(EXEC_ALARM_SOFT_LIMIT); // Indicate soft limit critical event
    protocol_execute_realtime(); // Execute to enter critical event loop and system abort
    return;
  }
}

#if defined(CPU_MAP_2560_RENKFORCE_CTC_V3) && RF2000_CNC
// Deliberate limit withdrawal, not homing and not a resumed job. A critical
// hard-limit alarm must first be reset so the old stream/planner is discarded.
uint8_t rf2000_limits_recover(void)
{
  const uint8_t selected = rf2000_limits_raw_state();
  if (sys.state != STATE_ALARM || sys.abort || sys_rt_exec_alarm ||
      (sys_rt_exec_state & EXEC_RESET) ||
      !rf2000_drivers_ready() || rf2000_drivers_fault_active() ||
      !selected || (selected & (selected - 1)) ||
      (rf2000_limit_held & (uint8_t)~selected)) return STATUS_SYSTEM_GC_LOCK;
  #if RF2000_LOCAL_PANEL
    if (rf2000_panel_stop_active()) return STATUS_SYSTEM_GC_LOCK;
  #endif

  const uint8_t axis = (selected & (RF2000_LIMIT_X_MIN | RF2000_LIMIT_Y_MIN)) ?
    (selected == RF2000_LIMIT_X_MIN ? AXIS_1 : AXIS_2) : AXIS_3;
  const float pulses = 2.0f * settings.steps_per_mm[axis];
  if (!isfinite(pulses) || pulses < 1.0f) return STATUS_INVALID_STATEMENT;
  // Two mm at F20 take six seconds: keep that requested pulse rate within
  // the tested ceiling and the planner's signed/AMASS integer range. A tiny
  // stored max_rate must not allow an extreme steps/mm value to overflow.
  if (pulses > MAX_STEP_RATE_HZ * 6.0f || pulses >= 2147483648.0f)
    return STATUS_MAX_STEP_RATE_EXCEEDED;
  int32_t origin[N_AXIS];
  float target[N_AXIS] = {0};
  target[axis] = selected == RF2000_LIMIT_Z_MAX ? -2.0f : 2.0f;
  plan_line_data_t move;
  memset(&move, 0, sizeof(move));
  move.condition = PL_COND_FLAG_SYSTEM_MOTION | PL_COND_FLAG_NO_FEED_OVERRIDE;
  move.feed_rate = 20.0f;

  st_reset();
  plan_reset();
  serial_reset_read_buffer();
  // Plan a strictly relative, single-axis displacement without float rounding
  // at large machine coordinates. Preserve the counters, but never certify
  // them as a reference after a hard stop.
  memcpy(origin, sys_position, sizeof(origin));
  memset(sys_position, 0, sizeof(origin));
  memset(sys.homing_axis_lock, 0, sizeof(sys.homing_axis_lock));
  sys.homing_axis_lock[axis] = get_step_pin_mask(axis);
  rf2000_panel_note_alarm(EXEC_ALARM_HARD_LIMIT); // Invalidate all references.
  rf2000_panel_homing_begin(bit(axis));
  const uint8_t saved_sreg = SREG;
  cli();
  rf2000_homing_expected = selected;
  rf2000_recovering = true;
  sys.state = STATE_HOMING; // Existing protected system-motion/axis-lock path.
  SREG = saved_sreg;
  system_clear_exec_state_flag(EXEC_CYCLE_STOP | EXEC_CYCLE_START);

  if (plan_buffer_line(target, &move)) {
    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION;
    st_prep_buffer();
    st_wake_up();
    for (;;) {
      // Full protocol execution here would consume CYCLE_STOP and enter IDLE,
      // prematurely removing the exception for the initially asserted input.
      const uint8_t stop = EXEC_RESET | EXEC_FEED_HOLD | EXEC_MOTION_CANCEL | EXEC_SAFETY_DOOR | EXEC_SLEEP;
      if (sys_rt_exec_alarm || (sys_rt_exec_state & stop)) {
        if (!sys_rt_exec_alarm) system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
        mc_reset();
        break;
      }
      if (sys_rt_exec_state & EXEC_STATUS_REPORT) {
        report_realtime_status();
        system_clear_exec_state_flag(EXEC_STATUS_REPORT);
      }
      if (sys_rt_exec_state & EXEC_CYCLE_STOP) {
        system_clear_exec_state_flag(EXEC_CYCLE_STOP);
        break;
      }
      st_prep_buffer();
      rf2000_panel_service();
    }
  } else system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_PULLOFF);

  st_reset();
  // Allow a contact that released at the last step to complete its normal
  // five-free-sample debounce. No further step may be emitted in this wait.
  for (uint8_t wait = 0; wait < 6 && (rf2000_limits_raw_state() | rf2000_limit_held) &&
       !sys_rt_exec_alarm && !(sys_rt_exec_state & EXEC_RESET); ++wait) {
    _delay_ms(1);
    rf2000_panel_service();
  }
  if (!sys_rt_exec_alarm && (sys_rt_exec_state &
      (EXEC_RESET | EXEC_FEED_HOLD | EXEC_MOTION_CANCEL | EXEC_SAFETY_DOOR | EXEC_SLEEP))) {
    system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
  }
  if (!sys_rt_exec_alarm && (rf2000_limits_raw_state() | rf2000_limit_held)) {
    system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_PULLOFF);
  }
  if (sys_rt_exec_alarm) mc_reset();
  rf2000_drivers_disable();
  const uint8_t finish_sreg = SREG;
  cli();
  rf2000_homing_expected = 0;
  rf2000_recovering = false;
  sys.state = STATE_ALARM;
  sys.step_control = STEP_CONTROL_NORMAL_OP;
  memset(sys.homing_axis_lock, 0, sizeof(sys.homing_axis_lock));
  SREG = finish_sreg;
  for (uint8_t i = 0; i < N_AXIS; ++i) {
    const int64_t position = (int64_t)origin[i] + sys_position[i];
    sys_position[i] = position > INT32_MAX ? INT32_MAX : position < INT32_MIN ? INT32_MIN : (int32_t)position;
  }
  plan_reset();
  gc_sync_position();
  plan_sync_position();
  serial_reset_read_buffer();
  if (sys_rt_exec_alarm || (sys_rt_exec_state & EXEC_RESET)) {
    protocol_execute_realtime(); // Preserve specific ISR alarms and reset behavior.
    return STATUS_SYSTEM_GC_LOCK;
  }
  printPgmString(PSTR("[MSG:RF2000 limit released; home again]\r\n"));
  return STATUS_OK; // Remain ALARM. No startup script, homing flag or job resume.
}
#endif
