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
#define KEY_CONTINUE (1u << 9)
#define KEY_PAUSE    (1u << 10)
#define STOP_KEYS (KEY_LEFT | KEY_CONTINUE | KEY_PAUSE)
#define KEY_DEBOUNCE_MS 20
#define MENU_ITEMS 5

static volatile uint16_t panel_ms;
static volatile uint8_t key_event, keys_ready, dispatch_busy;
static volatile uint16_t candidate_keys;
static volatile uint8_t candidate_ms, stop_was_down, initialized;
static uint8_t selection, confirm_home, diagnostics;
static uint8_t last_alarm, homing_phase, command_error;
static uint8_t dirty, frame_slot;
static uint16_t last_service_ms;
static uint8_t previous_state, previous_limits, previous_fault, previous_ready;
static uint8_t previous_keys_ready;
static char frame[80];

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
  confirm_home = diagnostics = command_error = homing_phase = 0;
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
  if (stop_down && !stop_was_down) {
    key_event = 0;
    keys_ready = false;
    mc_reset(); // Same immediate GPIO shutdown as serial Ctrl-X; no LCD/SPI.
  }
  stop_was_down = stop_down;
  if (keys != candidate_keys) {
    candidate_keys = keys;
    candidate_ms = 0;
  } else if (candidate_ms < KEY_DEBOUNCE_MS) {
    ++candidate_ms;
    if (candidate_ms == KEY_DEBOUNCE_MS) {
      if (!keys) {
        keys_ready = true;
      } else {
        const uint8_t accept = keys_ready && !dispatch_busy &&
          (sys.state == STATE_IDLE || sys.state == STATE_ALARM) &&
          !(sys_rt_exec_state & EXEC_RESET) && !sys_rt_exec_alarm;
        keys_ready = false; // Every action requires a full stable release.
        if (accept && !(keys & (keys - 1))) {
          if (keys == KEY_OK) key_event = 1;
          else if (keys == KEY_DOWN) key_event = 2;
          else if (keys == KEY_RIGHT) key_event = 3;
          else if (keys == KEY_UP) key_event = 4;
        }
      }
    }
  }
  if (dispatch_busy || (sys.state != STATE_IDLE && sys.state != STATE_ALARM)) {
    key_event = 0; // Never queue a movement while another action is running.
  }
}

uint8_t rf2000_panel_stop_active(void)
{
  // Also gate UART admission while STOP is held, including at power-up.
  return !(PINB & _BV(PB5)) || !(PINH & _BV(PH2)) || !(PINH & _BV(PH7));
}

void rf2000_panel_note_alarm(uint8_t alarm)
{
  if (alarm) {
    if (last_alarm != alarm || confirm_home) dirty = true;
    last_alarm = alarm;
    confirm_home = false;
  }
}

void rf2000_panel_homing_phase(uint8_t phase)
{
  homing_phase = phase;
  dirty = true;
}

static void row_P(uint8_t row, PGM_P text)
{
  uint8_t col = 0;
  char c;
  while (col < 20 && (c = pgm_read_byte(text++))) frame[row * 20 + col++] = c;
}

static void two_digits(uint8_t position, uint8_t value)
{
  frame[position] = '0' + (value / 10) % 10;
  frame[position + 1] = '0' + value % 10;
}

static void compose_frame(uint8_t limits, uint8_t ready)
{
  memset(frame, ' ', sizeof(frame));
  if (confirm_home) {
    row_P(0, PSTR("Y-Referenz starten?"));
    row_P(1, PSTR("Richtung geprueft?"));
    row_P(2, PSTR("Schalter geprueft?"));
    row_P(3, PSTR("OK:Start LINKS:STOP"));
    return;
  }
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
  if (diagnostics) {
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
  row_P(1, ready ? PSTR("DRV:OK YMIN:") : PSTR("DRV:FEHL YMIN:"));
  uint8_t col = ready ? 12 : 14;
  PGM_P state = (limits & 2) ? PSTR("aktiv") : PSTR("frei");
  char c;
  while ((c = pgm_read_byte(state++)) && col < 20) frame[20 + col++] = c;
  if (!keys_ready) row_P(2, PSTR("Tasten loslassen"));
  else if (command_error) {
    row_P(2, PSTR("Befehl gesperrt E00"));
    two_digits(57, command_error);
  } else {
    static const char labels[MENU_ITEMS][20] PROGMEM = {
      ">Entriegeln", ">Y -0.1 mm", ">Y +0.1 mm", ">Y referenzieren", ">Abbruch / Reset"
    };
    row_P(2, labels[selection]);
  }
  row_P(3, PSTR("OK:Wahl LINKS:STOP"));
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
  if (confirm_home && sys.state != STATE_IDLE && sys.state != STATE_ALARM) {
    confirm_home = false; // A UART-originated move invalidates local consent.
    dirty = true;
  }
  if (sys_rt_exec_alarm) rf2000_panel_note_alarm(sys_rt_exec_alarm);
  if (sys.state == STATE_ALARM) {
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
    ++frame_slot;
  }
}

static uint8_t execute(const char *text)
{
  char command[28];
  strcpy(command, text);
  return system_execute_line(command);
}

void rf2000_panel_dispatch(uint8_t command_boundary)
{
  uint8_t event;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { event = key_event; key_event = 0; }
  if (!command_boundary || !event || dispatch_busy || sys.abort ||
      sys_rt_exec_alarm || (sys_rt_exec_state & EXEC_RESET) ||
      (sys.state != STATE_IDLE && sys.state != STATE_ALARM)) return;
  dirty = true;
  command_error = 0;
  if (event == 2 || event == 4) {
    confirm_home = diagnostics = 0;
    selection = (selection + (event == 2 ? 1 : MENU_ITEMS - 1)) % MENU_ITEMS;
  } else if (event == 3) {
    confirm_home = 0;
    diagnostics = !diagnostics;
  } else if (event == 1) {
    if (diagnostics) { diagnostics = false; return; }
    dispatch_busy = true;
    if (confirm_home) {
      confirm_home = false;
      command_error = execute("$22=1");
      if (!command_error) command_error = execute("$HY");
    } else if (selection == 0) {
      command_error = execute("$X");
      if (!command_error) last_alarm = 0;
    } else if (selection == 1) command_error = execute("$J=G91G21Y-0.1F20");
    else if (selection == 2) command_error = execute("$J=G91G21Y0.1F20");
    else if (selection == 3) {
      if (limits_motion_allowed()) confirm_home = true;
      else command_error = STATUS_SYSTEM_GC_LOCK;
    } else mc_reset();
    dispatch_busy = false;
    dirty = true;
  }
}

#endif
