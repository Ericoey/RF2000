/* RF2000 adaptive execution, included by stepper.c. GPL-3.0-or-later.
 * Foreground: fixed fine-grid geometry -> phase-aware physical segments.
 * ISR: bounded 16-bit DDA, no floating point, division, SPI or busy waiting.
 */
#include "rf2000_adaptive_math.h"

/* Phase is distinct from GRBL's rebasable coordinates. Every used lattice
 * divides 256, so only these low bits of the 1024-state electrical cycle
 * are needed. The one hardware RESET in drivers_init starts at phase128. */
static volatile uint8_t rf_ad_phase[3] = {128,128,128};
static uint8_t rf_ad_first[3], rf_ad_stride, rf_ad_negative, rf_ad_phase_plus;
static uint16_t rf_ad_counter[3];
static volatile uint8_t rf_ad_mode_wait;
static volatile uint8_t rf_ad_tail_pending;
static uint8_t rf_ad_wait_shift, rf_ad_tail_block;
static uint8_t rf_ad_predict_valid, rf_ad_prepare_shift, rf_ad_homing_shift;
static uint32_t rf_ad_ideal_counter[3];
static int32_t rf_ad_ideal[3], rf_ad_physical[3];
static uint8_t rf_ad_predicted_phase[3];

static void rf_adaptive_geometry_fault(void)
{
  rf2000_stop_step_outputs();
  mc_reset();
  system_set_exec_alarm(EXEC_ALARM_ABORT_CYCLE);
}

/* Called only once an actual GPIO rising edge has been emitted, with IRQs
 * disabled. Homing masks have already been applied. Never account a pulse
 * that was only queued, masked, or cancelled by a delayed-edge fault. */
static inline __attribute__((always_inline)) void rf_adaptive_account_axis(uint8_t axis)
{
  uint8_t delta = rf_ad_first[axis];
  if (!delta) delta = rf_ad_stride;
  rf_ad_first[axis] = 0;
  if (rf_ad_negative & bit(axis)) sys_position[axis] -= delta;
  else sys_position[axis] += delta;
  if (rf_ad_phase_plus & bit(axis)) rf_ad_phase[axis] += delta;
  else rf_ad_phase[axis] -= delta;
}

static inline __attribute__((always_inline)) void rf_adaptive_account_edges(uint8_t bits)
{
  if (bits & _BV(PF0)) rf_adaptive_account_axis(0);
  if (bits & _BV(PF6)) rf_adaptive_account_axis(1);
  if (bits & _BV(PF3)) rf_adaptive_account_axis(2);
}

/* MODE is changed only in foreground, after Timer1 requests a pulse-idle
 * barrier. Timer2, UART and INT6 remain enabled throughout the transaction.
 * A partial transaction never permits the old segment metadata to run. */
static void rf_adaptive_service_mode(void)
{
  if (!rf_ad_mode_wait || TCCR0B) return;
  if (sys.abort || sys_rt_exec_alarm || (sys_rt_exec_state & EXEC_RESET)) {
    rf2000_stop_step_outputs();
    rf_ad_mode_wait = 0;
    return;
  }
  if (rf_ad_mode_wait == 2) {
    if (segment_buffer_head == segment_buffer_tail) return;
  } else if (!rf2000_drivers_set_microstep_shift(rf_ad_wait_shift)) {
    if (rf2000_drivers_fault_active() || !rf2000_drivers_ready())
      rf2000_stepper_fault_stop();
    else rf2000_stop_step_outputs();
    rf_ad_mode_wait = 0;
    return;
  }
  const uint8_t saved = SREG;
  cli();
  if (!sys.abort && !sys_rt_exec_alarm && !(sys_rt_exec_state & EXEC_RESET) &&
      rf2000_drivers_ready() && !rf2000_drivers_fault_active()) {
    rf_ad_mode_wait = 0;
    rf2000_direction_pending = true;
    TCNT1 = 0;
    OCR1A = F_CPU / RF_ADAPTIVE_PULSE_HZ;
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
  } else rf2000_stop_step_outputs();
  SREG = saved;
}

static void rf_adaptive_snapshot(void)
{
  const uint8_t saved = SREG;
  cli();
  for (uint8_t axis=0; axis<3; ++axis) {
    rf_ad_ideal[axis] = rf_ad_physical[axis] = sys_position[axis];
    rf_ad_predicted_phase[axis] = rf_ad_phase[axis];
  }
  SREG = saved;
  rf_ad_prepare_shift = 0;
  rf_ad_homing_shift = 3; /* Default reference profile remains fixed 1/32. */
  if (sys.state == STATE_HOMING && !rf2000_limits_recovery_active()) {
    const float rate = max(settings.homing_feed_rate,settings.homing_seek_rate)/60.0f;
    for (uint8_t axis=0; axis<3; ++axis)
      while (rf_ad_homing_shift < RF_ADAPTIVE_MAX_SHIFT &&
          settings.steps_per_mm[axis]*rate >
          (float)(RF_ADAPTIVE_PULSE_HZ*(1UL<<rf_ad_homing_shift)))
        ++rf_ad_homing_shift;
  }
  rf_ad_predict_valid = true;
}

/* Fill one segment from absolute ideal endpoints. Residual distance remains
 * in ideal-physical and is carried across segments and continuous blocks.
 * Floor in the block's direction prevents artificial backwards chatter. */
static bool rf_adaptive_plan_physical(segment_t *segment, uint8_t shift,
                                      uint8_t negative, bool exact_tail)
{
  segment->adaptive_shift = shift;
  segment->adaptive_negative = negative;
  uint16_t ticks = 0;
  for (uint8_t axis=0; axis<3; ++axis) {
    rf_adaptive_axis_t result;
    if (!rf_adaptive_axis_plan(rf_ad_ideal[axis],rf_ad_physical[axis],
        rf_ad_predicted_phase[axis],shift,(negative & bit(axis)) != 0,
        (settings.dir_invert_mask & bit(axis)) != 0,&result)) return false;
    /* In an ordinary block an axis with no geometry must remain stationary.
     * A stop tail may resolve a residual from the preceding moving block. */
    if (!exact_tail && !st_prep_block->steps[axis]) {
      segment->adaptive_pulses[axis] = 0;
      segment->adaptive_first[axis] = 0;
      continue;
    }
    segment->adaptive_pulses[axis] = result.pulses;
    segment->adaptive_first[axis] = result.first_delta;
    rf_ad_physical[axis] = result.position;
    rf_ad_predicted_phase[axis] = result.phase;
    if (result.pulses > ticks) ticks = result.pulses;
  }
  segment->n_step = ticks ? ticks : 1;
  return true;
}

static bool rf_adaptive_publish_tail(void)
{
  if (!rf_ad_tail_pending) return true;
  if (segment_buffer_tail == segment_next_head) return false;
  segment_t *segment = &segment_buffer[segment_buffer_head];
  uint8_t negative = 0;
  for (uint8_t axis=0; axis<3; ++axis)
    if (rf_ad_ideal[axis] < rf_ad_physical[axis]) negative |= bit(axis);
  segment->st_block_index = rf_ad_tail_block;
  segment->spindle_pwm = prep.current_spindle_pwm;
  if (!rf_adaptive_plan_physical(segment,0,negative,true)) {
    rf_adaptive_geometry_fault();
    return false;
  }
  /* The profile has reached zero velocity. Resolve only its tiny retained
   * substep, at <=6000 fine pulses/s. Never flush after an alarm/reset. */
  segment->cycles_per_tick = (F_CPU+5999UL)/6000UL;
  segment_buffer_head = segment_next_head;
  if (++segment_next_head == SEGMENT_BUFFER_SIZE) segment_next_head = 0;
  rf_ad_tail_pending = 0; /* Publish before clearing: ISR cannot finish early. */
  return true;
}

static bool rf_adaptive_prepare_segment(segment_t *segment,uint16_t virtual_steps,
                                         uint32_t cycles_per_virtual_step)
{
  uint8_t negative = 0;
  for (uint8_t axis=0; axis<3; ++axis) {
    const uint64_t sum = rf_ad_ideal_counter[axis] +
      (uint64_t)virtual_steps*st_prep_block->steps[axis];
    const uint32_t count = sum ? (sum-1u)/st_prep_block->step_event_count : 0;
    rf_ad_ideal_counter[axis] = sum - (uint64_t)count*st_prep_block->step_event_count;
    const bool neg = st_prep_block->direction_bits[axis] != 0;
    if (neg) negative |= bit(axis);
    const int64_t endpoint = (int64_t)rf_ad_ideal[axis] + (neg ? -(int64_t)count : count);
    if (endpoint < INT32_MIN || endpoint > INT32_MAX) return false;
    rf_ad_ideal[axis] = endpoint;
  }
  float fine_rate = (float)F_CPU/(float)cycles_per_virtual_step;
  if (!isfinite(fine_rate) || fine_rate > RF_ADAPTIVE_PULSE_HZ*32.0f)
    return false;
  uint8_t shift;
  if (sys.state == STATE_HOMING)
    shift = rf2000_limits_recovery_active() ? 0 : rf_ad_homing_shift;
  else shift = rf_adaptive_select_shift(fine_rate,rf_ad_prepare_shift);
  rf_ad_prepare_shift = shift;
  if (!rf_adaptive_plan_physical(segment,shift,negative,false)) return false;
  if (virtual_steps && cycles_per_virtual_step > UINT32_MAX/virtual_steps)
    return false;
  uint32_t duration = (uint32_t)virtual_steps*cycles_per_virtual_step;
  uint16_t ticks = segment->n_step;
  /* Oversample the physical DDA when useful, rather than running one ISR
   * for every virtual 1/256 unit. Keep 16-bit timer intervals representable. */
  uint32_t minimum_ticks = (duration+59999UL)/60000UL;
  if (minimum_ticks > UINT16_MAX/2u || ticks > UINT16_MAX/2u) return false;
  if (minimum_ticks > ticks) ticks = minimum_ticks;
  const uint32_t budget_ticks = duration/(F_CPU/RF_ADAPTIVE_PULSE_HZ+1UL);
  while ((uint32_t)ticks*2u <= budget_ticks && ticks <= UINT16_MAX/4u)
    ticks *= 2u;
  uint32_t interval = (duration+ticks-1u)/ticks;
  const uint16_t minimum_interval = (F_CPU+RF_ADAPTIVE_PULSE_HZ-1UL)/RF_ADAPTIVE_PULSE_HZ;
  if (interval < minimum_interval) interval = minimum_interval;
  if (interval > 60000UL) interval = 60000UL;
  segment->n_step = ticks;
  segment->cycles_per_tick = interval;
  return true;
}

static inline __attribute__((always_inline)) void rf_adaptive_tick_axis(
    uint8_t axis,uint8_t pin,uint8_t *bits)
{
  rf_ad_counter[axis] += st.exec_segment->adaptive_pulses[axis];
  if (rf_ad_counter[axis] >= st.exec_segment->n_step) {
    rf_ad_counter[axis] -= st.exec_segment->n_step;
    if (sys.state != STATE_HOMING || (sys.homing_axis_lock[axis] & pin))
      *bits |= pin;
  }
}

static inline void rf_adaptive_tick(void)
{
  if (!rf2000_drivers_ready() || rf2000_drivers_fault_active()) {
    rf2000_stepper_fault_stop();
    return;
  }
  if (sys_rt_exec_alarm || (sys_rt_exec_state & EXEC_RESET)) {
    rf2000_stop_step_outputs();
    return;
  }
  if (TCCR0B) { rf_adaptive_geometry_fault(); return; }
  if (!st.exec_segment) {
    if (segment_buffer_head == segment_buffer_tail) {
      if (rf_ad_tail_pending) {
        TIMSK1 &= ~_BV(OCIE1A);
        rf_ad_mode_wait = 2;
      } else {
        /* st_go_idle may honor a non-255 idle dwell. Timer1 is already
         * stopped; keep safety/UART interrupts live throughout that dwell. */
        TIMSK1 &= ~_BV(OCIE1A);
        sei();
        st_go_idle();
        cli();
        system_set_exec_state_flag(EXEC_CYCLE_STOP);
      }
      return;
    }
    segment_t *segment = &segment_buffer[segment_buffer_tail];
    if (rf2000_drivers_microstep_shift() != segment->adaptive_shift) {
      rf_ad_wait_shift = segment->adaptive_shift;
      TIMSK1 &= ~_BV(OCIE1A);
      rf_ad_mode_wait = 1;
      return;
    }
    st.exec_segment = segment;
    st.exec_block_index = segment->st_block_index;
    st.exec_block = &st_block_buffer[st.exec_block_index];
    st.step_count = segment->n_step;
    rf_ad_stride = 1u << segment->adaptive_shift;
    rf_ad_negative = segment->adaptive_negative;
    rf_ad_phase_plus = 0;
    for (uint8_t axis=0; axis<3; ++axis) {
      rf_ad_counter[axis] = segment->n_step/2u;
      rf_ad_first[axis] = segment->adaptive_first[axis];
      const bool neg = (rf_ad_negative & bit(axis)) != 0;
      st.dir_outbits[axis] = (neg ? get_direction_pin_mask(axis) : 0) ^ dir_port_invert_mask[axis];
      if (((settings.dir_invert_mask & bit(axis)) != 0) != neg)
        rf_ad_phase_plus |= bit(axis);
    }
    const uint8_t direction_f = st.dir_outbits[0] | st.dir_outbits[1];
    const uint8_t direction_l = st.dir_outbits[2];
    if (!rf2000_direction_valid || rf2000_direction_f != direction_f || rf2000_direction_l != direction_l) {
      rf2000_write_directions(direction_f,0,direction_l);
      rf2000_direction_f = direction_f;
      rf2000_direction_l = direction_l;
      rf2000_direction_valid = true;
      rf2000_direction_pending = true;
    }
    OCR1A = segment->cycles_per_tick;
    TCNT1 = 0;
    spindle_set_speed(segment->spindle_pwm);
  }
  uint8_t bits = 0;
  rf_adaptive_tick_axis(0,_BV(PF0),&bits);
  rf_adaptive_tick_axis(1,_BV(PF6),&bits);
  rf_adaptive_tick_axis(2,_BV(PF3),&bits);
  if (bits) {
    st.step_bits[0] = bits;
    TCNT0 = 0;
    if (rf2000_direction_pending) {
      TIFR0 = RF2000_TIMER0_FLAGS;
      TIMSK0 = _BV(OCIE0A);
      TCCR0B = _BV(CS01);
    } else {
      TCCR0B = _BV(CS01);
      rf2000_write_steps(bits);
      rf_adaptive_account_edges(bits);
      TCNT0 = (uint8_t)(-rf2000_pulse_ticks);
      TIFR0 = RF2000_TIMER0_FLAGS;
      TIMSK0 = _BV(TOIE0);
    }
  }
  if (--st.step_count == 0) {
    st.exec_segment = NULL;
    if (++segment_buffer_tail == SEGMENT_BUFFER_SIZE) segment_buffer_tail = 0;
  }
}
