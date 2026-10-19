#!/usr/bin/env python3
"""Exercise actual serial.c with mocked UART registers and realtime callbacks.

Compile with warnings as errors. Cover both 256-byte rings (natural uint8_t
wrap) and smaller rings, including every initial head/tail position. Hardware
interrupt timing is outside this host test.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
constants = []
for filename in ("config.h", "system.h"):
    for line in (root / "grbl" / filename).read_text().splitlines():
        if re.match(r"^#define (?:CMD_|EXEC_|STATE_JOG\b)", line):
            constants.append(line)

mock = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "serial.h"
#define bit(n) (1u << (n))
#define CPU_MAP_2560_RENKFORCE_CTC_V3
#define F_CPU 16000000UL
#define BAUD_RATE 115200UL
#define U2X0 1
#define RXEN0 4
#define TXEN0 3
#define RXCIE0 7
#define UDRIE0 5
#define SERIAL_UDRE serial_udre_interrupt
#define SERIAL_RX serial_rx_interrupt
#define ISR(name) void name(void)
static uint8_t UCSR0A, UCSR0B, UBRR0H, UBRR0L, UDR0;
static uint8_t sys_rt_exec_state, alarm, motion_override, accessory_override;
static struct { uint8_t state; } sys;
static unsigned resets;
static void mc_reset(void) { resets++; sys_rt_exec_state |= EXEC_RESET; }
static void system_set_exec_state_flag(uint8_t flag) { sys_rt_exec_state |= flag; }
static void system_set_exec_alarm(uint8_t flag) { alarm = flag; }
static void system_set_exec_motion_override_flag(uint8_t flag) { motion_override |= flag; }
static void system_set_exec_accessory_override_flag(uint8_t flag) { accessory_override |= flag; }
'''

tests = r'''
#include "serial.c"

static void receive(uint8_t data)
{
  UDR0 = data;
  serial_rx_interrupt();
}

static uint8_t letter(unsigned index) { return 'A' + index % 26; }

static void rx_count(unsigned count)
{
  assert(serial_get_rx_buffer_count() == count);
  assert(serial_get_rx_buffer_available() == RX_BUFFER_SIZE - count);
}

static void test_rx(void)
{
  // Exercise every starting ring position. Filling, partially draining and
  // refilling also checks FIFO order through the wrap boundary.
  for (unsigned start = 0; start < RX_RING_BUFFER; start++) {
    serial_rx_buffer_head = serial_rx_buffer_tail = start;
    sys_rt_exec_state = 0; resets = 0; alarm = 0;
    rx_count(0);
    assert(serial_read() == SERIAL_NO_DATA);
    for (unsigned i = 0; i < RX_BUFFER_SIZE; i++) {
      receive(letter(i));
      rx_count(i + 1);
    }
    uint8_t head = serial_rx_buffer_head;
    // Realtime commands bypass even a full RX ring.
    receive(CMD_STATUS_REPORT);
    assert(sys_rt_exec_state == EXEC_STATUS_REPORT);
    receive(CMD_RESET);
    assert(resets == 1 && (sys_rt_exec_state & EXEC_RESET));
    assert(serial_rx_buffer_head == head && alarm == 0);
    rx_count(RX_BUFFER_SIZE);
    // A normal character at capacity cannot overwrite a queued character.
    sys_rt_exec_state = 0;
    receive('Z');
    assert(resets == 2 && (sys_rt_exec_state & EXEC_RESET));
    assert(alarm == EXEC_ALARM_SERIAL_RX_OVERFLOW);
    assert(serial_rx_buffer_head == head);
    rx_count(RX_BUFFER_SIZE);

    const unsigned chunk = (RX_BUFFER_SIZE + 1) / 2;
    for (unsigned i = 0; i < chunk; i++) {
      assert(serial_read() == letter(i));
      rx_count(RX_BUFFER_SIZE - i - 1);
    }
    for (unsigned i = 0; i < chunk; i++) {
      receive(letter(RX_BUFFER_SIZE + i));
      rx_count(RX_BUFFER_SIZE - chunk + i + 1);
    }
    for (unsigned i = chunk; i < RX_BUFFER_SIZE + chunk; i++) {
      assert(serial_read() == letter(i));
      rx_count(RX_BUFFER_SIZE + chunk - i - 1);
    }
    assert(serial_read() == SERIAL_NO_DATA);

    receive('G');
    receive(CMD_FEED_OVR_FINE_PLUS);
    receive(CMD_COOLANT_FLOOD_OVR_TOGGLE);
    assert(motion_override & EXEC_FEED_OVR_FINE_PLUS);
    assert(accessory_override & EXEC_COOLANT_FLOOD_OVR_TOGGLE);
    rx_count(1);
    serial_reset_read_buffer();
    rx_count(0);
    assert(serial_read() == SERIAL_NO_DATA);
    // Jog cancellation flushes queued bytes immediately.
    receive('Y'); sys.state = STATE_JOG;
    receive(CMD_JOG_CANCEL);
    assert(sys_rt_exec_state & EXEC_MOTION_CANCEL);
    rx_count(0); sys.state = 0;
  }
}

static void test_tx(void)
{
  for (unsigned start = 0; start < TX_RING_BUFFER; start++) {
    serial_tx_buffer_head = serial_tx_buffer_tail = start;
    sys_rt_exec_state = 0; UCSR0B = 0;
    assert(serial_get_tx_buffer_count() == 0);
    for (unsigned i = 0; i < TX_BUFFER_SIZE; i++) {
      serial_write((uint8_t)i);
      assert(serial_get_tx_buffer_count() == i + 1);
      assert(UCSR0B & bit(UDRIE0));
    }
    // A pending reset breaks a full-buffer wait without adding data.
    uint8_t head = serial_tx_buffer_head;
    sys_rt_exec_state = EXEC_RESET;
    serial_write(0xff);
    assert(serial_tx_buffer_head == head);
    assert(serial_get_tx_buffer_count() == TX_BUFFER_SIZE);
    sys_rt_exec_state = 0;

    const unsigned chunk = (TX_BUFFER_SIZE + 1) / 2;
    for (unsigned i = 0; i < chunk; i++) {
      serial_udre_interrupt();
      assert(UDR0 == (uint8_t)i);
      assert(serial_get_tx_buffer_count() == TX_BUFFER_SIZE - i - 1);
    }
    for (unsigned i = 0; i < chunk; i++) {
      serial_write((uint8_t)(TX_BUFFER_SIZE + i));
      assert(serial_get_tx_buffer_count() == TX_BUFFER_SIZE - chunk + i + 1);
    }
    for (unsigned i = chunk; i < TX_BUFFER_SIZE + chunk; i++) {
      serial_udre_interrupt();
      assert(UDR0 == (uint8_t)i);
      unsigned count = TX_BUFFER_SIZE + chunk - i - 1;
      assert(serial_get_tx_buffer_count() == count);
      assert(!!(UCSR0B & bit(UDRIE0)) == !!count);
    }
  }
}

int main(void)
{
  serial_init();
  assert(UCSR0A & bit(U2X0));
  assert(UCSR0B == (bit(RXEN0) | bit(TXEN0) | bit(RXCIE0)));
  assert(UBRR0H == 0 && UBRR0L == 16);
  test_rx();
  test_tx();
  printf("Serial RX=%u TX=%u: all ring positions, FIFO, counts, overflow/reset and realtime bypass PASS\n",
         (unsigned)RX_BUFFER_SIZE, (unsigned)TX_BUFFER_SIZE);
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="rf2000-serial-") as directory:
    directory = Path(directory)
    (directory / "serial.c").write_text((root / "grbl/serial.c").read_text())
    (directory / "serial.h").write_text((root / "grbl/serial.h").read_text())
    (directory / "grbl.h").write_text("\n".join(constants) + "\n" + mock)
    c_file = directory / "serial_host.c"
    c_file.write_text(tests)
    executable = directory / "serial_host"
    command = [os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2"]
    for rx_size, tx_size in ((255, 255), (128, 64), (1, 1), (254, 255), (255, 2)):
        subprocess.run(command + [f"-DRX_BUFFER_SIZE={rx_size}", f"-DTX_BUFFER_SIZE={tx_size}",
                                 str(c_file), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

    for name in ("RX_BUFFER_SIZE", "TX_BUFFER_SIZE"):
        for size in (0, 256, -1):
            result = subprocess.run(command + [f"-D{name}={size}", "-fsyntax-only", str(c_file)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert result.returncode != 0, (name, size)
            assert f"{name} must be between 1 and 255" in result.stdout, result.stdout
    print("Serial ring compile-time capacity guards (RX/TX 0, 256, -1): PASS")
