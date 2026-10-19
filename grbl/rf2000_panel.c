/*
 * Local Y-test controls for the RF2000v2 20x4 LCD and original keypad.
 * GPIO mapping and active-LOW input handling derived from RF Repetier
 * pins.h (RF2000v2) and ui.cpp:566-593. GPL-3.0-or-later.
 * No printer menus or heater/extruder actions are imported.
 */
#include "grbl.h"
#include "rf2000_lcd.h"
#include <util/atomic.h>

#if RF2000_LOCAL_PANEL

#define KEY_OK       (1u << 0)
#define KEY_DOWN     (1u << 1)
#define KEY_RIGHT    (1u << 2)
#define KEY_LEFT     (1u << 3)
#define KEY_UP       (1u << 4)
#define KEY_STEP_UP  (1u << 5)
#define KEY_STEP_DOWN (1u << 6)
#define KEY_FEED_UP  (1u << 7)
#define KEY_FEED_DOWN (1u << 8)
#define KEY_CONTINUE (1u << 9)
#define KEY_PAUSE    (1u << 10)
#if RF2000_CNC
#define STOP_KEYS KEY_PAUSE
#else
#define STOP_KEYS (KEY_LEFT | KEY_CONTINUE | KEY_PAUSE)
#endif
#define KEY_DEBOUNCE_MS 20
#if RF2000_CNC
#define MENU_ITEMS 8
#else
#define MENU_ITEMS 5
#endif
#define PAGE_MAIN 0
#define PAGE_JOG 1
#define PAGE_DIAGNOSTICS 2
#define PAGE_MENU 3
#define PAGE_REFERENCE 4
#define PAGE_ZERO 5
#define PAGE_SPEED 6

static volatile uint16_t panel_ms;
static volatile uint8_t key_event, keys_ready, dispatch_busy;
static volatile uint16_t candidate_keys;
static volatile uint8_t candidate_ms, stop_was_down, initialized;
static uint8_t selection, confirm_home, page, step_index, feed_index, referenced;
#if RF2000_CNC
static uint8_t selected_axis, homing_axes;
static uint8_t subselection, candidate_busy;
static uint16_t pause_ticks;
static uint8_t pause_reset_latched;
#endif
static uint8_t last_alarm, homing_phase, command_error;
static uint8_t dirty, frame_slot;
static uint16_t last_service_ms;
static uint16_t last_position_ms;
static int32_t displayed_steps;
static uint8_t previous_state, previous_limits, previous_fault, previous_ready;
static uint8_t previous_keys_ready;
static char frame[80];
static const char step_labels[3][5] PROGMEM = {"0.01", "0.10", "0.50"};
static const uint8_t jog_feeds[3] = {10, 20, 30};

static uint16_t read_keys(void)
{
  const uint8_t pg = PING, pb = PINB, ph = PINH, pl = PINL, pj = PINJ;
  uint16_t keys = 0;
  if (!(pg & _BV(PG5))) keys |= KEY_OK;
  if (!(pb & _BV(PB7))) keys |= KEY_DOWN;
  if (!(ph & _BV(PH0))) keys |= KEY_RIGHT;
  if (!(pb & _BV(PB5))) keys |= KEY_LEFT;
  if (!(pl & _BV(PL7))) keys |= KEY_UP;
  if (!(pj & _BV(PJ2))) keys |= (1u << 5);
  if (!(pj & _BV(PJ4))) keys |= (1u << 6);
  if (!(pj & _BV(PJ5))) keys |= (1u << 7);
  if (!(pj & _BV(PJ6))) keys |= (1u << 8);
  if (!(ph & _BV(PH7))) keys |= KEY_CONTINUE;
  if (!(ph & _BV(PH2))) keys |= KEY_PAUSE;
  return keys;
}

void rf2000_panel_init(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    DDRG &= ~_BV(PG5); PORTG |= _BV(PG5);
    DDRB &= ~(_BV(PB5) | _BV(PB7)); PORTB |= _BV(PB5) | _BV(PB7);
    DDRH &= ~(_BV(PH0) | _BV(PH2) | _BV(PH7));
    PORTH |= _BV(PH0) | _BV(PH2) | _BV(PH7);
    DDRL &= ~_BV(PL7); PORTL |= _BV(PL7);
    DDRJ &= ~(_BV(PJ2) | _BV(PJ4) | _BV(PJ5) | _BV(PJ6));
    PORTJ |= _BV(PJ2) | _BV(PJ4) | _BV(PJ5) | _BV(PJ6);
    stop_was_down = !!(read_keys() & STOP_KEYS);
  }
  rf2000_lcd_init(); // Startup only, while every motor is asleep.
  initialized = true;
}

void rf2000_panel_reset(void)
{
  if (!initialized) return;
  if (sys_rt_exec_alarm) last_alarm = sys_rt_exec_alarm;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    key_event = 0;
    keys_ready = false;
    candidate_keys = read_keys();
    candidate_ms = 0;
    dispatch_busy = false;
    // Preserve stop_was_down across soft reset: holding STOP is not a loop.
  }
  selection = 0;
  confirm_home = page = command_error = homing_phase = referenced = 0;
  step_index = feed_index = 1;
  #if RF2000_CNC
    selected_axis = AXIS_1;
    homing_axes = 0;
    subselection = 0;
    candidate_busy = false;
  #endif
  last_position_ms = 0;
  previous_state = previous_limits = previous_fault = previous_ready = 0xff;
  previous_keys_ready = 0xff;
  dirty = true;
}

void rf2000_panel_tick_isr(void)
{
  if (!initialized) return;
  ++panel_ms;
  const uint16_t keys = read_keys();
  const uint8_t stop_down = !!(keys & STOP_KEYS);
  #if RF2000_CNC
    if (stop_down && !stop_was_down) {
      pause_ticks = 0;
      // A fresh II edge must always stop, even if another key stayed held
      // since the previous reset chord. Only suppress repeated held-II resets.
      pause_reset_latched = false;
      key_event = 0;
      keys_ready = false;
      if (sys.state == STATE_CYCLE || sys.state == STATE_HOLD) {
        system_set_exec_state_flag(EXEC_FEED_HOLD);
      } else if (sys.state == STATE_JOG) {
        system_set_exec_state_flag(EXEC_MOTION_CANCEL);
      } else {
        pause_reset_latched = true;
        mc_reset(); // Homing/recovery has no resumable feed hold.
      }
    }
    if (stop_down) {
      if (pause_ticks < 1000) ++pause_ticks;
      if (!pause_reset_latched && (pause_ticks == 1000 || (keys & KEY_CONTINUE))) {
        pause_reset_latched = true;
        mc_reset(); // Hold II for one second, or press > and II together.
      }
    }
  #else
  if (stop_down && !stop_was_down) {
    key_event = 0;
    keys_ready = false;
    mc_reset(); // Same immediate GPIO shutdown as serial Ctrl-X; no LCD/SPI.
  }
  #endif
  stop_was_down = stop_down;
  if (keys != candidate_keys) {
    candidate_keys = keys;
    candidate_ms = 0;
    #if RF2000_CNC
      candidate_busy = sys.state != STATE_IDLE && sys.state != STATE_ALARM;
    #endif
  } else if (candidate_ms < KEY_DEBOUNCE_MS) {
    ++candidate_ms;
    if (candidate_ms == KEY_DEBOUNCE_MS) {
      if (!keys) {
        keys_ready = true;
        #if RF2000_CNC
          pause_ticks = 0;
          pause_reset_latched = false;
        #endif
      } else {
        #if RF2000_CNC
          const uint8_t stable = keys_ready && !(keys & (keys - 1)) &&
            !(sys_rt_exec_state & EXEC_RESET) && !sys_rt_exec_alarm;
          if (stable && (sys.state == STATE_CYCLE || sys.state == STATE_HOLD)) {
            if (keys == KEY_FEED_UP) system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS);
            else if (keys == KEY_FEED_DOWN) system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS);
            else if (keys == KEY_CONTINUE && sys.state == STATE_HOLD &&
                     (sys.suspend & SUSPEND_HOLD_COMPLETE)) system_set_exec_state_flag(EXEC_CYCLE_START);
          }
        #endif
        const uint8_t accept = keys_ready && !dispatch_busy &&
          (sys.state == STATE_IDLE || sys.state == STATE_ALARM) &&
          !(sys_rt_exec_state & EXEC_RESET) && !sys_rt_exec_alarm;
        keys_ready = false; // Every action requires a full stable release.
        if (accept && !(keys & (keys - 1))
            #if RF2000_CNC
              && !candidate_busy
            #endif
            ) {
          if (keys == KEY_OK) key_event = 1;
          else if (keys == KEY_DOWN) key_event = 2;
          else if (keys == KEY_RIGHT) key_event = 3;
          else if (keys == KEY_UP) key_event = 4;
          else if (keys == KEY_STEP_UP) key_event = 5;
          else if (keys == KEY_STEP_DOWN) key_event = 6;
          else if (keys == KEY_FEED_UP) key_event = 7;
          else if (keys == KEY_FEED_DOWN) key_event = 8;
          #if RF2000_CNC
            else if (keys == KEY_LEFT) key_event = 9;
            else if (keys == KEY_CONTINUE) key_event = 10;
          #endif
        }
      }
    }
  }
  if (dispatch_busy || (sys.state != STATE_IDLE && sys.state != STATE_ALARM)) {
    key_event = 0; // Never queue a movement while another action is running.
    // Reject at the first busy sample, even if a very short jog ends before
    // this new key has completed its 20-ms debounce interval.
    #if RF2000_CNC
      if (keys && !(keys & (KEY_CONTINUE | KEY_FEED_UP | KEY_FEED_DOWN))) keys_ready = false;
    #else
      if (keys) keys_ready = false;
    #endif
  }
}

uint8_t rf2000_panel_stop_active(void)
{
  // Also gate UART admission while STOP is held, including at power-up.
  #if RF2000_CNC
    return !(PINH & _BV(PH2));
  #else
  return !(PINB & _BV(PB5)) || !(PINH & _BV(PH2)) || !(PINH & _BV(PH7));
  #endif
}

void rf2000_panel_note_alarm(uint8_t alarm)
{
  if (alarm) {
    #if RF2000_CNC
      if (page != PAGE_MAIN || command_error) dirty = true;
      page = PAGE_MAIN;
      command_error = 0;
    #endif
    if (last_alarm != alarm || confirm_home || referenced) dirty = true;
    last_alarm = alarm;
    confirm_home = false;
    referenced = false;
  }
}

void rf2000_panel_homing_phase(uint8_t phase)
{
  homing_phase = phase;
  #if !RF2000_CNC
    if (phase == 1) referenced = false;
  #endif
  dirty = true;
}

void rf2000_panel_homing_complete(uint8_t axes)
{
  if (!sys.abort && !sys_rt_exec_alarm && !(sys_rt_exec_state & EXEC_RESET)) {
    #if RF2000_CNC
      referenced |= axes;
    #else
      (void)axes;
      referenced = true;
    #endif
    dirty = true;
  }
}

#if RF2000_CNC
void rf2000_panel_homing_begin(uint8_t axes)
{
  homing_axes = axes;
  referenced &= ~axes;
  dirty = true;
}

void rf2000_panel_homing_axes(uint8_t axes)
{
  homing_axes = axes; // Actual Z or XY cycle, not the whole $H request.
  dirty = true;
}
#endif

static void row_P(uint8_t row, PGM_P text)
{
  #if RF2000_CNC
    memset(frame + row * 20, ' ', 20);
  #endif
  uint8_t col = 0;
  char c;
  while (col < 20 && (c = pgm_read_byte(text++))) frame[row * 20 + col++] = c;
}

static void two_digits(uint8_t position, uint8_t value)
{
  frame[position] = '0' + (value / 10) % 10;
  frame[position + 1] = '0' + value % 10;
}

static void position_row(void)
{
  const uint8_t axis =
    #if RF2000_CNC
      selected_axis;
    #else
      AXIS_2;
    #endif
  int32_t steps;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { steps = sys_position[axis]; }
  // Y-test settings are fixed at 152.38 steps/mm. Limit the display field
  // before formatting; never negate INT32_MIN or overrun a 20-column row.
  float millimeters = (float)steps / settings.steps_per_mm[axis];
  #if RF2000_CNC
    millimeters -= gc_state.coord_system[axis] + gc_state.coord_offset[axis];
    if (axis == AXIS_3) millimeters -= gc_state.tool_length_offset;
  #endif
  float centi_mm = millimeters * 100.0f;
  if (centi_mm > 99999900.0f || centi_mm < -99999900.0f) {
    row_P(1, PSTR("Y: ausser Bereich"));
    return;
  }
  int32_t centi = lroundf(centi_mm);
  uint32_t magnitude = centi < 0 ? (uint32_t)-centi : (uint32_t)centi;
  char number[12], *out = number;
  if (centi < 0) *out++ = '-';
  ultoa(magnitude / 100, out, 10);
  out += strlen(out);
  *out++ = '.';
  *out++ = '0' + (magnitude / 10) % 10;
  *out++ = '0' + magnitude % 10;
  uint8_t length = out - number;
  frame[20] = 'X' + axis; frame[21] = ':';
  memcpy(frame + 32 - length, number, length);
  frame[32] = 'm'; frame[33] = 'm';
  frame[35] = 'R'; frame[36] = ':';
  uint8_t known =
    #if RF2000_CNC
      referenced & bit(axis);
    #else
      referenced;
    #endif
  frame[37] = known ? 'J' : '?';
  if (known) frame[38] = 'A';
}

static void driver_row(uint8_t row, uint8_t limits, uint8_t ready)
{
  row_P(row, ready ? PSTR("DRV:OK YMIN:") : PSTR("DRV:FEHL YMIN:"));
  uint8_t col = ready ? 12 : 14;
  PGM_P state = (limits & 2) ? PSTR("aktiv") : PSTR("frei");
  char c;
  while ((c = pgm_read_byte(state++)) && col < 20) frame[row * 20 + col++] = c;
}

#if RF2000_CNC
static void decimal_field(uint8_t offset, float mm)
{
  if (!isfinite(mm) || mm > 9999.99f || mm < -9999.99f) {
    memcpy_P(frame + offset, PSTR("OVERFLOW"), 8);
    return;
  }
  int32_t centi = lroundf(mm * 100.0f);
  uint32_t magnitude = centi < 0 ? (uint32_t)-centi : (uint32_t)centi;
  char number[12], *out = number;
  if (centi < 0) *out++ = '-';
  ultoa(magnitude / 100, out, 10);
  out += strlen(out);
  *out++ = '.'; *out++ = '0' + (magnitude / 10) % 10; *out++ = '0' + magnitude % 10;
  uint8_t length = out - number;
  memcpy(frame + offset + 8 - length, number, length);
}

static void compose_cnc_frame(uint8_t limits, uint8_t ready)
{
  row_P(0, PSTR("CNC BEREIT   G54 ---"));
  PGM_P state = PSTR("BEREIT  ");
  if (sys.state == STATE_ALARM) state = PSTR("ALARM00 ");
  else if (sys.state == STATE_HOMING) state = rf2000_limits_recovery_active() ? PSTR("RUECKZUG") : PSTR("REFERENZ");
  else if (sys.state == STATE_CYCLE) state = PSTR("FAHRT   ");
  else if (sys.state & STATE_HOLD) state = (sys.suspend & SUSPEND_HOLD_COMPLETE) ?
    PSTR("HALT    ") : PSTR("BREMSEN ");
  else if (sys.state == STATE_JOG) state = PSTR("JOG     ");
  else if (sys.state == STATE_CHECK_MODE) state = PSTR("PRUEFEN ");
  memcpy_P(frame + 4, state, 8);
  if (sys.state == STATE_ALARM) two_digits(9, last_alarm);
  frame[15] = '4' + gc_state.modal.coord_select;
  for (uint8_t axis = 0; axis < 3; ++axis) frame[17 + axis] = (referenced & bit(axis)) ? 'X' + axis : '-';
  if (sys.state == STATE_HOMING) {
    if (rf2000_limits_recovery_active()) {
      row_P(1, PSTR("FREIFAHREN ---"));
      for (uint8_t axis = 0; axis < 3; ++axis) frame[31 + axis] = (homing_axes & bit(axis)) ? 'X' + axis : '-';
      row_P(2, PSTR("2mm max, F20"));
      row_P(3, PSTR("II:Abbruch SP:MAN."));
      return;
    }
    row_P(0, PSTR("REF "));
    uint8_t col = 4;
    for (uint8_t axis = 0; axis < 3; ++axis) if (homing_axes & bit(axis)) {
      if (col > 4) frame[col++] = '+';
      frame[col++] = 'X' + axis;
    }
    frame[col++] = ':'; frame[col++] = ' ';
    PGM_P phase = homing_phase == 1 ? PSTR("SUCHEN") : homing_phase == 2 ?
      PSTR("RUECKZUG") : homing_phase == 3 ? PSTR("FEINANFAHRT") : PSTR("FREIFAHREN");
    char c;
    while (col < 20 && (c = pgm_read_byte(phase++))) frame[col++] = c;
    row_P(1, (homing_axes & bit(AXIS_3)) ?
      ((settings.homing_dir_mask & bit(AXIS_3)) ? PSTR("Schalter: Z-MIN") : PSTR("Schalter: Z-MAX")) :
      homing_axes == bit(AXIS_1) ? PSTR("Schalter: X-MIN") : homing_axes == bit(AXIS_2) ?
      PSTR("Schalter: Y-MIN") : PSTR("X-MIN und Y-MIN"));
    row_P(2, PSTR("F:"));
    char speed[12];
    const float rate = homing_phase == 3 ? settings.homing_feed_rate : settings.homing_seek_rate;
    ultoa((uint32_t)fminf(999999.0f, fmaxf(0.0f, rate)), speed, 10);
    memcpy(frame + 42, speed, strlen(speed));
    memcpy_P(frame + 42 + strlen(speed), PSTR(" mm/min"), 7);
    row_P(3, PSTR("II:Abbruch SP:MAN."));
    return;
  }
  if (page == PAGE_DIAGNOSTICS && (sys.state == STATE_IDLE || sys.state == STATE_ALARM)) {
    row_P(1, PSTR("L X0 Y0 Z-0 Z+0"));
    frame[23] = '0' + !!(limits & 1); frame[26] = '0' + !!(limits & 2);
    frame[30] = '0' + !!(limits & 4); frame[34] = '0' + !!(limits & 8);
    row_P(2, PSTR("DRV F00 R00 A00"));
    two_digits(45, rf2000_drivers_fault_reason());
    two_digits(49, rf2000_drivers_readback_failed_mask()); two_digits(53, last_alarm);
    row_P(3, PSTR("OK/<:Zurueck"));
    return;
  }
  if ((sys.state == STATE_IDLE || sys.state == STATE_ALARM) &&
      (page == PAGE_MENU || page == PAGE_REFERENCE || page == PAGE_ZERO || page == PAGE_SPEED)) {
    if (page == PAGE_SPEED) {
      row_P(0, PSTR("JOG-VORSCHUB"));
      row_P(1, PSTR("F:20 mm/min")); two_digits(22, jog_feeds[feed_index]);
      row_P(2, PSTR("+/- oder Hoch/Runter"));
    } else {
      static const char labels[MENU_ITEMS][20] PROGMEM = {"Freigeben", "Verfahren XYZ",
        "Referenzieren", "Arbeitsnull", "Diagnose", "Endschalter frei", "Jogtempo", "Abbruch / Reset"};
      static const char axes[4][20] PROGMEM = {"Alle: X/Y/Z", "Nur X", "Nur Y", "Nur Z"};
      row_P(0, page == PAGE_MENU ? PSTR("CNC MENUE") : page == PAGE_REFERENCE ? PSTR("REFERENZIEREN") : PSTR("ARBEITSNULL SETZEN"));
      const uint8_t chosen = page == PAGE_MENU ? selection : subselection;
      const uint8_t count = page == PAGE_MENU ? MENU_ITEMS : 4;
      row_P(1, page == PAGE_MENU ? labels[chosen] : axes[chosen]);
      row_P(2, page == PAGE_MENU ? labels[(chosen + 1) % count] : axes[(chosen + 1) % count]);
      for (int8_t i = 19; i > 0; --i) frame[20 + i] = frame[20 + i - 1];
      frame[20] = '>';
    }
    row_P(3, PSTR("OK:Wahl <:Zurueck"));
    return;
  }
  int32_t steps[N_AXIS];
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { memcpy(steps, sys_position, sizeof(steps)); }
  float work[3];
  for (uint8_t axis = 0; axis < 3; ++axis) {
    work[axis] = (float)steps[axis] / settings.steps_per_mm[axis] -
      gc_state.coord_system[axis] - gc_state.coord_offset[axis];
  }
  work[AXIS_3] -= gc_state.tool_length_offset;
  frame[20] = 'X'; frame[21] = ':'; decimal_field(22, work[0]);
  frame[30] = 'Y'; frame[31] = ':'; decimal_field(32, work[1]);
  frame[40] = 'Z'; frame[41] = ':'; decimal_field(42, work[2]);
  frame[51] = 'F'; frame[52] = ':';
  float rate = st_get_realtime_rate();
  if (!isfinite(rate) || rate < 0) rate = 0;
  if (rate > 999999) rate = 999999;
  char feed[8]; ultoa((uint32_t)lroundf(rate), feed, 10);
  memcpy(frame + 59 - strlen(feed), feed, strlen(feed));
  if (page == PAGE_JOG && sys.state != STATE_CYCLE && sys.state != STATE_HOLD) {
    row_P(0, sys.state == STATE_ALARM ? PSTR("JOG GESPERRT") : sys.state == STATE_JOG ?
      PSTR("JOG ---    S0.10 F20") : PSTR("JOG BEREIT S0.10 F20"));
    if (sys.state != STATE_ALARM) {
      memcpy_P(frame + 12, step_labels[step_index], 4); two_digits(18, jog_feeds[feed_index]);
      if (sys.state == STATE_JOG) {
        const uint8_t axes = rf2000_stepper_active_axes();
        for (uint8_t axis = 0; axis < 3; ++axis) frame[4 + axis] = (axes & bit(axis)) ? 'X' + axis : '-';
      }
    }
    row_P(3, PSTR("OK:Menue +/-:Weg"));
  } else if (sys.state == STATE_IDLE || sys.state == STATE_ALARM) {
    row_P(3, PSTR("OK:Menue SP:MANUELL"));
  } else {
    if (sys.state == STATE_CYCLE || sys.state == STATE_JOG) {
      row_P(0, sys.state == STATE_CYCLE ? PSTR("CNC FAHRT ") : PSTR("CNC JOG "));
      const uint8_t axes = rf2000_stepper_active_axes();
      uint8_t col = sys.state == STATE_CYCLE ? 10 : 8;
      for (uint8_t axis = 0; axis < 3; ++axis) if (axes & bit(axis)) frame[col++] = 'X' + axis;
      frame[17] = 'G'; frame[18] = '5'; frame[19] = '4' + gc_state.modal.coord_select;
    }
    row_P(3, PSTR("II:Pause >:Weiter"));
  }
  (void)ready;
}
#endif

static void compose_frame(uint8_t limits, uint8_t ready)
{
  memset(frame, ' ', sizeof(frame));
  if (command_error) {
    row_P(0, PSTR("AKTION GESPERRT E00"));
    two_digits(17, command_error);
    if (!ready) row_P(1, PSTR("Treiber pruefen"));
    else if (limits) row_P(1, PSTR("Endschalter aktiv"));
    else if (sys.state == STATE_ALARM) row_P(1, PSTR("Erst freigeben"));
    else row_P(1, PSTR("Befehl abgewiesen"));
    row_P(2, PSTR("Keine Bewegung"));
    #if RF2000_CNC
      row_P(3, PSTR("OK/>/<:Zurueck"));
    #else
      row_P(3, PSTR("OK/>:Zurueck <-STOP"));
    #endif
    return;
  }
  if (confirm_home) {
    #if RF2000_CNC
      if (confirm_home == 10) {
        row_P(0, PSTR("Abbruch / Reset?")); row_P(1, PSTR("Referenz wird unklar"));
      } else if (confirm_home == 3) {
        row_P(0, PSTR("Endschalter frei?")); row_P(1, PSTR("2mm zurueck, F20"));
      } else if (confirm_home == 2 || confirm_home >= 7) {
        row_P(0, PSTR("Arbeitsnull setzen?"));
        row_P(1, confirm_home == 2 ? PSTR("X/Y/Z hier = 0") : PSTR("Nur X hier = 0"));
        if (confirm_home >= 7) frame[24] = 'X' + confirm_home - 7;
      } else {
        row_P(0, confirm_home == 1 ? PSTR("XYZ referenzieren?") : PSTR("X referenzieren?"));
        if (confirm_home >= 4) frame[0] = 'X' + confirm_home - 4;
        row_P(1, confirm_home == 1 ? PSTR("Zmax dann X/Ymin") : confirm_home == 6 ?
          PSTR("Zum Z-Max-Schalter") : PSTR("Zum Min-Schalter"));
      }
      row_P(2, PSTR("Spindel manuell"));
    #else
    row_P(0, PSTR("Y-Referenz starten?"));
    row_P(1, PSTR("Richtung geprueft?"));
    row_P(2, PSTR("Schalter geprueft?"));
    #endif
    row_P(3,
      #if RF2000_CNC
        PSTR("OK:Start <:Abbruch")
      #else
        PSTR("OK:Start LINKS:STOP")
      #endif
    );
    return;
  }
  #if RF2000_CNC
    compose_cnc_frame(limits, ready);
    return;
  #endif
  if (sys.state == STATE_HOMING) {
    if (homing_phase == 1) row_P(0, PSTR("Y: REF-SUCHE"));
    else if (homing_phase == 3) row_P(0, PSTR("Y: FEINANFAHRT"));
    else if (homing_phase == 4) row_P(0, PSTR("Y: FREIFAHREN"));
    else row_P(0, PSTR("Y: RUECKZUG"));
  } else if (sys.state == STATE_JOG) row_P(0, PSTR("Y: JOG"));
  else if (sys.state == STATE_ALARM) {
    row_P(0, PSTR("RF2000 Y: ALARM 00"));
    two_digits(16, last_alarm);
  } else row_P(0, PSTR("RF2000 Y: BEREIT"));
  if (page == PAGE_DIAGNOSTICS && sys.state != STATE_HOMING) {
    row_P(1, PSTR("L X0 Y0 Z-0 Z+0"));
    frame[23] = '0' + !!(limits & 1);
    frame[26] = '0' + !!(limits & 2);
    frame[30] = '0' + !!(limits & 4);
    frame[34] = '0' + !!(limits & 8);
    row_P(2, PSTR("DRV F00 R00 A00"));
    two_digits(45, rf2000_drivers_fault_reason());
    two_digits(49, rf2000_drivers_readback_failed_mask());
    two_digits(53, last_alarm);
    row_P(3, PSTR("RECHTS:Menue <-STOP"));
    return;
  }
  // Homing rebases its internal step counter at each search block. It is
  // not a continuous machine coordinate until the reference cycle succeeds.
  if (sys.state == STATE_HOMING) row_P(1, PSTR("Y wird referenziert"));
  else position_row();
  if (sys.state == STATE_HOMING) {
    driver_row(2, limits, ready);
    row_P(3, PSTR("LINKS:STOP"));
  } else if (page == PAGE_JOG) {
    if (sys.state != STATE_JOG) row_P(0, sys.state == STATE_ALARM ?
      PSTR("Y VERFAHREN GESPERRT") : PSTR("Y VERFAHREN BEREIT"));
    row_P(2, PSTR("Weg:0.10mm F:20"));
    memcpy_P(frame + 44, step_labels[step_index], 4);
    two_digits(53, jog_feeds[feed_index]);
    row_P(3, PSTR("HO:Y+ RU:Y- OK:Weg"));
  } else {
    static const char labels[MENU_ITEMS][20] PROGMEM = {
      ">Freigeben", ">Y verfahren", ">Y referenzieren", ">Diagnose", ">Abbruch / Reset"
    };
    row_P(2, labels[selection]);
    driver_row(3, limits, ready);
  }
}

void rf2000_panel_service(void)
{
  if (!initialized) return;
  uint16_t now;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { now = panel_ms; }
  // Different tick numbers alone can be only microseconds apart. Two tick
  // increments guarantee >=1 ms between bytes, even across an ISR boundary.
  if ((uint16_t)(now - last_service_ms) < 2) return;
  last_service_ms = now;
  const uint8_t limits = rf2000_limits_raw_state();
  const uint8_t fault = rf2000_drivers_fault_reason();
  const uint8_t ready = rf2000_drivers_ready() && !fault;
  if ((uint16_t)(now - last_position_ms) >= 200) {
    int32_t steps;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { steps = sys_position[AXIS_2]; }
    last_position_ms = now;
    if (steps != displayed_steps) { displayed_steps = steps; dirty = true; }
    #if RF2000_CNC
      dirty = true; // WCO, feed and all three coordinates may change via gSender.
    #endif
  }
  if (confirm_home && sys.state != STATE_IDLE && sys.state != STATE_ALARM) {
    confirm_home = false; // A UART-originated move invalidates local consent.
    dirty = true;
  }
  #if RF2000_CNC
    if (sys.state != STATE_IDLE && sys.state != STATE_ALARM) {
      command_error = 0; // A stale menu error must not hide a running action.
      if (sys.state != STATE_JOG && page != PAGE_MAIN) { page = PAGE_MAIN; dirty = true; }
    }
  #endif
  if (sys_rt_exec_alarm) rf2000_panel_note_alarm(sys_rt_exec_alarm);
  if (sys.state == STATE_ALARM) {
    if (referenced) { referenced = false; dirty = true; }
    if (!ready) last_alarm = EXEC_ALARM_DRIVER_FAULT;
    else if (limits && !last_alarm) last_alarm = EXEC_ALARM_HARD_LIMIT;
  }
  if (sys.state != previous_state || limits != previous_limits ||
      fault != previous_fault || ready != previous_ready || keys_ready != previous_keys_ready) {
    previous_state = sys.state; previous_limits = limits;
    previous_fault = fault; previous_ready = ready; previous_keys_ready = keys_ready;
    dirty = true;
  }
  if (dirty) { compose_frame(limits, ready); frame_slot = 0; dirty = false; }
  if (frame_slot < 84) {
    static const uint8_t row_address[4] = {0x00, 0x40, 0x14, 0x54};
    const uint8_t row = frame_slot / 21, col = frame_slot % 21;
    if (!col) rf2000_lcd_write(0, 0x80 | row_address[row]);
    else rf2000_lcd_write(1, frame[row * 20 + col - 1]);
    // Compose/formatting may span several ticks under STEP interrupt load.
    // Space from the completed transfer, not from before that foreground work.
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { last_service_ms = panel_ms; }
    ++frame_slot;
  }
}

static uint8_t execute(const char *text)
{
  char command[28];
  strcpy(command, text);
  return system_execute_line(command);
}

static uint8_t jog(uint8_t positive)
{
  char command[] = "$J=G91G21Y+0.10F20";
  #if RF2000_CNC
    command[9] = 'X' + selected_axis;
  #endif
  command[10] = positive ? '+' : '-';
  memcpy_P(command + 11, step_labels[step_index], 4);
  command[16] = '0' + jog_feeds[feed_index] / 10;
  command[17] = '0' + jog_feeds[feed_index] % 10;
  return system_execute_line(command);
}

#if RF2000_CNC
static void dispatch_cnc(uint8_t event)
{
  if (command_error) {
    if (event == 1 || event == 3 || event == 9) command_error = 0;
    return;
  }
  if (confirm_home) {
    if (event == 9) { confirm_home = 0; return; }
    if (event != 1 && event != 3) return;
    const uint8_t action = confirm_home;
    confirm_home = 0;
    page = PAGE_MAIN;
    dispatch_busy = true;
    if (action == 10) mc_reset();
    else if (action == 3) command_error = execute("$LR");
    else if (action == 2 || action >= 7) {
      char command[24] = "G10L20P1";
      command[7] = '1' + gc_state.modal.coord_select;
      if (action == 2) strcpy(command + 8, "X0Y0Z0");
      else { command[8] = 'X' + action - 7; command[9] = '0'; command[10] = 0; }
      command_error = sys.state == STATE_IDLE ? gc_execute_line(command) : STATUS_SYSTEM_GC_LOCK;
    } else {
      char command[] = "$HX";
      if (action == 1) command[2] = 0;
      else command[2] = 'X' + action - 4;
      command_error = execute(command);
    }
    dispatch_busy = false;
    return;
  }
  if (page == PAGE_MAIN) {
    if (event == 1 || event == 3) page = PAGE_MENU;
    else if (event == 5 || event == 6) page = PAGE_JOG; // First press only opens jog.
    else if (event == 10 && sys.state == STATE_ALARM) command_error = STATUS_SYSTEM_GC_LOCK;
    return;
  }
  if (page == PAGE_DIAGNOSTICS) {
    if (event == 1 || event == 3 || event == 9) page = PAGE_MENU;
    return;
  }
  if (page == PAGE_JOG) {
    if (event == 1) page = PAGE_MENU;
    else if (event == 7 && step_index) --step_index; // Physical minus (PJ5).
    else if (event == 8 && step_index < 2) ++step_index; // Physical plus (PJ6).
    else if (event == 2 || event == 3 || event == 4 || event == 5 || event == 6 || event == 9) {
      selected_axis = (event == 3 || event == 9) ? AXIS_1 : (event == 2 || event == 4) ? AXIS_2 : AXIS_3;
      dispatch_busy = true;
      command_error = jog(event == 3 || event == 4 || event == 5);
      dispatch_busy = false;
    }
    return;
  }
  if (page == PAGE_SPEED) {
    if (event == 4 || event == 8) { if (feed_index < 2) ++feed_index; }
    else if (event == 2 || event == 7) { if (feed_index) --feed_index; }
    else if (event == 1 || event == 3 || event == 9) page = PAGE_MENU;
    return;
  }
  if (page == PAGE_REFERENCE || page == PAGE_ZERO) {
    if (event == 9) page = PAGE_MENU;
    else if (event == 2 || event == 4 || event == 7 || event == 8) {
      subselection = (subselection + ((event == 2 || event == 8) ? 1 : 3)) % 4;
    } else if (event == 1 || event == 3) {
      if (page == PAGE_REFERENCE) {
        if (limits_motion_allowed()) confirm_home = subselection ? 3 + subselection : 1;
        else command_error = STATUS_SYSTEM_GC_LOCK;
      } else {
        if (sys.state == STATE_IDLE) confirm_home = subselection ? 6 + subselection : 2;
        else command_error = STATUS_SYSTEM_GC_LOCK;
      }
    }
    return;
  }
  if (event == 9) { page = PAGE_MAIN; return; }
  if (event == 2 || event == 4 || event == 7 || event == 8) {
    selection = (selection + ((event == 2 || event == 8) ? 1 : MENU_ITEMS - 1)) % MENU_ITEMS;
  } else if (event == 1 || event == 3) {
    if (selection == 0) {
      dispatch_busy = true;
      command_error = execute("$X");
      dispatch_busy = false;
      if (!command_error) { last_alarm = 0; page = PAGE_MAIN; }
    } else if (selection == 1) page = PAGE_JOG;
    else if (selection == 2 || selection == 3) {
      page = selection == 2 ? PAGE_REFERENCE : PAGE_ZERO;
      subselection = 0;
    } else if (selection == 4) page = PAGE_DIAGNOSTICS;
    else if (selection == 5) {
      if (sys.state == STATE_ALARM) confirm_home = 3;
      else command_error = STATUS_SYSTEM_GC_LOCK;
    } else if (selection == 6) page = PAGE_SPEED;
    else confirm_home = 10;
  }
}
#endif

void rf2000_panel_dispatch(uint8_t command_boundary)
{
  uint8_t event;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { event = key_event; key_event = 0; }
  if (!command_boundary || !event || dispatch_busy || sys.abort ||
      sys_rt_exec_alarm || (sys_rt_exec_state & EXEC_RESET) ||
      (sys.state != STATE_IDLE && sys.state != STATE_ALARM)) return;
  dirty = true;
  #if RF2000_CNC
    dispatch_cnc(event);
    return;
  #endif
  if (command_error) {
    if (event == 1 || event == 3) command_error = 0;
    return; // Acknowledging an error never executes the selected action.
  }
  if (page == PAGE_DIAGNOSTICS) {
    if (event == 1 || event == 3) page = PAGE_MAIN;
    return;
  }
  if (page == PAGE_JOG) {
    if (event == 3) page = PAGE_MAIN;
    else if (event == 1) {
      #if RF2000_CNC
        selected_axis = (selected_axis + 1) % 3;
      #else
        step_index = (step_index + 1) % 3;
      #endif
    }
    else if (event == 5 && step_index < 2) ++step_index;
    else if (event == 6 && step_index) --step_index;
    else if (event == 7 && feed_index < 2) ++feed_index;
    else if (event == 8 && feed_index) --feed_index;
    else if (event == 2 || event == 4) {
      dispatch_busy = true;
      command_error = jog(event == 4);
      dispatch_busy = false;
    }
    return;
  }
  if (event == 2 || event == 4) {
    confirm_home = 0;
    selection = (selection + (event == 2 ? 1 : MENU_ITEMS - 1)) % MENU_ITEMS;
  } else if (event == 3) {
    confirm_home = 0;
    page = PAGE_DIAGNOSTICS;
  } else if (event == 1) {
    dispatch_busy = true;
    if (confirm_home) {
      #if RF2000_CNC
        if (confirm_home == 3) {
          confirm_home = false;
          command_error = execute("$LR");
        } else if (confirm_home == 2) {
          char command[] = "G10L20P1X0Y0Z0";
          command[7] = '1' + gc_state.modal.coord_select;
          confirm_home = false;
          command_error = sys.state == STATE_IDLE ? gc_execute_line(command) : STATUS_SYSTEM_GC_LOCK;
        } else {
          confirm_home = false;
          command_error = execute("$H");
        }
      #else
      confirm_home = false;
      command_error = execute("$22=1");
      if (!command_error) command_error = execute("$HY");
      #endif
    } else if (selection == 0) {
      command_error = execute("$X");
      if (!command_error) last_alarm = 0;
    } else if (selection == 1) page = PAGE_JOG;
    else if (selection == 2) {
      if (limits_motion_allowed()) confirm_home = true;
      else command_error = STATUS_SYSTEM_GC_LOCK;
    #if RF2000_CNC
    } else if (selection == 3) {
      if (sys.state == STATE_IDLE) confirm_home = 2;
      else command_error = STATUS_SYSTEM_GC_LOCK;
    } else if (selection == 4) page = PAGE_DIAGNOSTICS;
    else if (selection == 5) {
      if (sys.state == STATE_ALARM) confirm_home = 3;
      else command_error = STATUS_SYSTEM_GC_LOCK;
    }
    #else
    } else if (selection == 3) page = PAGE_DIAGNOSTICS;
    #endif
    else mc_reset();
    dispatch_busy = false;
    dirty = true;
  }
}

#endif
