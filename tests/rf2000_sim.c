/* Run the real ATmega2560 ELF in simavr with a five-channel DRV8711 SPI
 * register model. This verifies digital firmware behavior, not real hardware,
 * power electronics, switch wiring or analog current regulation. GPL-3.0+. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"
#include "avr_uart.h"

#define CPU_HZ 16000000UL
#define LOG_SIZE 65536
#define CHECK(condition, ...) do { if (!(condition)) { \
  fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\nUART: %s\n", sim.log); exit(1); } } while (0)

typedef struct simulation simulation_t;
typedef struct { simulation_t *sim; char port; uint8_t bit; } watched_pin_t;
struct simulation {
  avr_t *avr;
  avr_irq_t *uart_in;
  char log[LOG_SIZE];
  size_t log_len;
  uint8_t pins[26][8];
  uint8_t input_mask[26], input_value[26];
  watched_pin_t watch[26][8];
  int selected;
  uint8_t spi_bits;
  uint16_t spi_word, regs[5][8];
  unsigned writes[5][8], reads[5][8];
  unsigned reset_count[5];
  unsigned step_count[5];
  uint64_t step_start[3], dir_changed[3];
  uint64_t step_window[3][64], min_window_cycles[3];
  unsigned window_count[3];
  uint64_t min_pulse, min_setup;
  int corrupt_readback;
  int verify_xy_synchronous;
  uint32_t timer1_entry, timer0_compare_entry, alarm_address;
};
static simulation_t sim;

static avr_irq_t *pin_irq(char port, uint8_t bit)
{
  return avr_io_getirq(sim.avr, AVR_IOCTL_IOPORT_GETIRQ(port), bit);
}

static void input_pin(char port, uint8_t bit, uint8_t value)
{
  unsigned p = port - 'A';
  sim.input_mask[p] |= 1u << bit;
  if (value) sim.input_value[p] |= 1u << bit;
  else sim.input_value[p] &= ~(1u << bit);
  avr_ioport_external_t external = {
    .name = (unsigned)port, .mask = sim.input_mask[p],
    .value = sim.input_value[p]
  };
  avr_ioctl(sim.avr, AVR_IOCTL_IOPORT_SET_EXTERNAL(port), &external);
  avr_raise_irq(pin_irq(port, bit), value);
}

static int is_output(char port, uint8_t bit)
{
  avr_ioport_state_t state = {0};
  avr_ioctl(sim.avr, AVR_IOCTL_IOPORT_GETSTATE(port), &state);
  return !!(state.ddr & (1u << bit));
}

static int selected_driver(char port, uint8_t bit)
{
  if (port == 'L' && bit == 0) return 0;
  if (port == 'L' && bit == 2) return 1;
  if (port == 'B' && bit == 6) return 2;
  if (port == 'L' && bit == 6) return 3;
  if (port == 'J' && bit == 7) return 4;
  return -1;
}

static int axis_step(char port, uint8_t bit)
{
  if (port == 'F' && bit == 0) return 0;
  if (port == 'F' && bit == 6) return 1;
  if (port == 'F' && bit == 3) return 2;
  if (port == 'A' && bit == 4) return 3;
  if (port == 'C' && bit == 1) return 4;
  return -1;
}

static int axis_direction(char port, uint8_t bit)
{
  if (port == 'F' && bit == 1) return 0;
  if (port == 'F' && bit == 7) return 1;
  if (port == 'L' && bit == 1) return 2;
  return -1;
}

static int awake(int axis)
{
  static const char port[5] = {'D','F','K','A','C'};
  static const uint8_t bit[5] = {7,2,0,2,7};
  return sim.pins[port[axis]-'A'][bit[axis]];
}

static int forbidden_output(char port, uint8_t bit)
{
  return (port == 'A' && (bit == 3 || bit == 5 || bit == 7)) ||
    (port == 'B' && bit == 4) ||
    (port == 'C' && (bit == 4 || bit == 5)) ||
    (port == 'E' && (bit == 2 || bit == 3 || bit == 5)) ||
    (port == 'F' && bit == 4) ||
    (port == 'H' && bit >= 3 && bit <= 6);
}

static void pin_changed(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq;
  watched_pin_t *watch = opaque;
  const char port = watch->port;
  const uint8_t bit = watch->bit, level = value & 1;
  const uint8_t previous = sim.pins[port-'A'][bit];
  sim.pins[port-'A'][bit] = level;
  if (!is_output(port, bit) || previous == level) return;
  CHECK(!(level && forbidden_output(port,bit)),
    "heater/auxiliary output P%c%u became HIGH",port,bit);

  if (port == 'E' && bit == 7 && level) {
    for (int d=0;d<5;++d) if (awake(d)) {
      memset(sim.regs[d],0,sizeof(sim.regs[d]));
      sim.regs[d][0] = 0x0c10; // Relevant RESET property: ENBL=0.
      ++sim.reset_count[d];
    }
  }

  const int driver = selected_driver(port,bit);
  if (driver >= 0) {
    if (level) {
      CHECK(sim.selected < 0, "multiple active-HIGH DRV chip selects");
      CHECK(!sim.pins['E'-'A'][7],"SPI attempted while RESET active");
      sim.selected = driver;
      sim.spi_bits = 0;
      sim.spi_word = 0;
    } else if (sim.selected == driver) {
      CHECK(sim.spi_bits == 16, "DRV%d SPI frame has %u bits",driver+1,sim.spi_bits);
      const unsigned address = (sim.spi_word >> 12) & 7;
      if (sim.spi_word & 0x8000) ++sim.reads[driver][address];
      else {
        uint16_t data = sim.spi_word & 0x0fff;
        if (address == 7 && !awake(driver))
          data |= sim.regs[driver][7] & 0x20; // UVLO cannot clear in SLEEP.
        sim.regs[driver][address] = data;
        CHECK(!(driver>=3 && awake(driver) && (sim.regs[driver][0]&1)),
          "reserve driver bridge energized");
        ++sim.writes[driver][address];
      }
      sim.selected = -1;
    }
  }
  if (port == 'D' && bit == 5 && sim.selected >= 0) {
    if (level) {
      CHECK(sim.spi_bits < 16, "more than 16 SPI clocks");
      sim.spi_word = (sim.spi_word << 1) | sim.pins['D'-'A'][6];
      ++sim.spi_bits;
    } else if (sim.spi_bits >= 4 && sim.spi_bits < 16) {
      // At four bits the header is complete. Retain it for the remaining
      // bits: spi_word shifts, so decode the current header position.
      const unsigned header = sim.spi_word >> (sim.spi_bits - 4);
      const unsigned reg = header & 7;
      uint16_t response = sim.regs[sim.selected][reg];
      if (sim.corrupt_readback && reg == 0) response ^= 1;
      input_pin('D',4,(response >> (15-sim.spi_bits)) & 1);
    }
  }
  const int axis = axis_step(port,bit);
  if (axis >= 0) {
    if (level) {
      CHECK(axis < 3, "reserve axis %d emitted STEP",axis);
      CHECK(awake(axis), "axis %d STEP occurred while asleep",axis);
      if (axis == 1 && sim.verify_xy_synchronous)
        CHECK(sim.step_start[0] == sim.avr->cycle,
          "equal XY trajectory did not produce simultaneous PORTF edges");
      ++sim.step_count[axis];
      const unsigned index = sim.window_count[axis]++ & 63;
      if (sim.step_window[axis][index]) {
        const uint64_t interval = sim.avr->cycle - sim.step_window[axis][index];
        if (!sim.min_window_cycles[axis] || interval < sim.min_window_cycles[axis])
          sim.min_window_cycles[axis] = interval;
      }
      sim.step_window[axis][index] = sim.avr->cycle;
      sim.step_start[axis] = sim.avr->cycle;
      const uint64_t setup = sim.avr->cycle - sim.dir_changed[axis];
      if (setup < sim.min_setup) sim.min_setup = setup;
      CHECK(setup >= 80, "axis%d DIR setup is %.3f us",axis,setup/16.0);
    } else if (axis < 3 && sim.step_start[axis]) {
      const uint64_t width = sim.avr->cycle - sim.step_start[axis];
      if (awake(axis) && width < sim.min_pulse) sim.min_pulse = width;
      CHECK(!awake(axis) || width >= 160,
        "axis%d ordinary STEP width is %.3f us",axis,width/16.0);
      sim.step_start[axis] = 0;
    }
  }
  const int direction_axis = axis_direction(port,bit);
  if (direction_axis >= 0) {
    CHECK(!sim.step_start[direction_axis], "DIR changed during active STEP");
    sim.dir_changed[direction_axis] = sim.avr->cycle;
  }
  if ((port == 'A' && bit == 2) || (port == 'C' && bit == 7))
    CHECK(!level || !(sim.regs[port=='A'?3:4][0]&1),
      "reserve driver woke with ENBL=1");
}

static void uart_output(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq; (void)opaque;
  CHECK(sim.log_len + 1 < sizeof(sim.log), "UART log overflow");
  sim.log[sim.log_len++] = (char)value;
  sim.log[sim.log_len] = 0;
}

static void run_us(uint64_t duration)
{
  const uint64_t until = sim.avr->cycle + duration * 16;
  while (sim.avr->cycle < until) {
    int state = avr_run(sim.avr);
    CHECK(state != cpu_Done && state != cpu_Crashed,
      "MCU stopped at cycle %" PRIu64 " state=%d",sim.avr->cycle,state);
  }
}

static const char *command(const char *text, unsigned wait_ms)
{
  const size_t start = sim.log_len;
  while (*text) {
    avr_raise_irq(sim.uart_in,(uint8_t)*text++);
    run_us(125);
  }
  run_us((uint64_t)wait_ms * 1000);
  return sim.log + start;
}

static void boot(const char *elf, int corrupt)
{
  memset(&sim,0,sizeof(sim));
  sim.selected = -1;
  sim.min_pulse = sim.min_setup = UINT64_MAX;
  sim.corrupt_readback = corrupt;
  // Model a retained old firmware configuration and latched initial UVLO.
  // SPI remains usable in SLEEP; RESET only affects awake channels.
  for (int d=0;d<5;++d) {
    sim.regs[d][0] = 0x0e29;
    sim.regs[d][7] = 0x20;
  }
  elf_firmware_t firmware = {0};
  CHECK(elf_read_firmware(elf,&firmware) == 0,"cannot read ELF %s",elf);
  for (unsigned i=0;i<firmware.symbolcount;++i) {
    const avr_symbol_t *symbol = firmware.symbol[i];
    if (!strcmp(symbol->symbol,"__vector_17")) sim.timer1_entry = symbol->addr;
    if (!strcmp(symbol->symbol,"__vector_21")) sim.timer0_compare_entry = symbol->addr;
    if (!strcmp(symbol->symbol,"sys_rt_exec_alarm")) sim.alarm_address = symbol->addr-0x800000;
  }
  CHECK(sim.timer1_entry && sim.timer0_compare_entry && sim.alarm_address,
    "ELF lacks ISR/alarm symbols needed for deterministic fault injection");
  sim.avr = avr_make_mcu_by_name("atmega2560");
  CHECK(sim.avr != NULL,"simavr lacks atmega2560");
  avr_init(sim.avr);
  sim.avr->frequency = CPU_HZ;
  avr_load_firmware(sim.avr,&firmware);
  uint32_t flags = 0;
  avr_ioctl(sim.avr,AVR_IOCTL_UART_SET_FLAGS('0'),&flags);
  sim.uart_in = avr_io_getirq(sim.avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_INPUT);
  avr_irq_register_notify(avr_io_getirq(sim.avr,AVR_IOCTL_UART_GETIRQ('0'),
    UART_IRQ_OUTPUT),uart_output,NULL);
  const char *ports = "ABCDEFGHJKL";
  for (const char *p=ports; *p; ++p) for (uint8_t bit=0;bit<8;++bit) {
    watched_pin_t *watch = &sim.watch[*p-'A'][bit];
    *watch = (watched_pin_t){.sim=&sim,.port=*p,.bit=bit};
    avr_irq_register_notify(pin_irq(*p,bit),pin_changed,watch);
  }
  input_pin('E',6,1); // FAULTn inactive
  input_pin('D',4,0); // DRV SDATO model
  input_pin('C',2,1); input_pin('C',0,1); input_pin('C',6,1);
  input_pin('H',1,1);
  for (uint8_t bit=0;bit<5;++bit) input_pin('G',bit,1);
  run_us(1200000);
  CHECK(strstr(sim.log,"Grbl"),"missing Grbl startup");
  // Repetier RF2000v2: PA7 is WDI, released as in HAL::stopWatchdog().
  // PC4 drives OUT3 LOW; PC0 stays the ES2/Y-Min limit input.
  avr_ioport_state_t porta = {0}, portc = {0};
  avr_ioctl(sim.avr,AVR_IOCTL_IOPORT_GETSTATE('A'),&porta);
  avr_ioctl(sim.avr,AVR_IOCTL_IOPORT_GETSTATE('C'),&portc);
  CHECK(!(porta.ddr & (1u<<7)) && !(porta.port & (1u<<7)),
    "disabled watchdog/PA7 must remain an input without pull-up");
  CHECK((portc.ddr & (1u<<4)) && !(portc.port & (1u<<4)),
    "OUT3/PC4 must drive inactive LOW");
  CHECK(!(portc.ddr & 1u),"PC0/ES2 must remain a limit input");
  CHECK(strstr(command("?",30),"<Alarm"),"startup must be alarm locked");
}

static void all_asleep(void)
{
  for (int axis=0;axis<5;++axis) CHECK(!awake(axis),"driver%d remains awake",axis+1);
}

static void assert_ok(const char *text, unsigned wait_ms)
{
  const char *reply = command(text,wait_ms);
  CHECK(strstr(reply,"ok\r\n") && !strstr(reply,"error:"),
    "command rejected: %s reply=%s",text,reply);
}

static void assert_error(const char *text)
{
  const char *reply = command(text,50);
  CHECK(strstr(reply,"error:"),"unsafe/invalid command accepted: %s reply=%s",text,reply);
}

static void verify_registers(void)
{
  #ifndef RF2000_TEST_XY_CTRL
    #define RF2000_TEST_XY_CTRL 0xe29
  #endif
  const uint16_t ctrl[5] = {RF2000_TEST_XY_CTRL,RF2000_TEST_XY_CTRL,0xe21,0xe28,0xe28};
  const uint16_t torque[5] = {0x16e,0x16e,0x169,0x15a,0x15a};
  const uint16_t common[8] = {0,0,0x97,0x1d7,0x430,0x83c,0xf0,0};
  for (int d=0;d<5;++d) for (int r=0;r<8;++r) {
    const uint16_t expected = r == 0 ? ctrl[d] : r == 1 ? torque[d] : common[r];
    CHECK(sim.regs[d][r] == expected,"DRV%d reg%d=%03x expected%03x",d+1,r,sim.regs[d][r],expected);
    const unsigned count = r==0 ? 3 : 1;
    CHECK(sim.writes[d][r] == count && sim.reads[d][r] == count,
      "DRV%d reg%d must be initialized and read back %u times",d+1,r,count);
    CHECK(sim.reset_count[d]==1,"DRV%d RESET must occur awake",d+1);
  }
}

static void inject_fault_at_isr(const char *elf, int delayed_edge)
{
  boot(elf,0);
  assert_ok("$X\n",30);
  command("G91 G1 X10 F100\n",0);
  const uint32_t entry = delayed_edge ? sim.timer0_compare_entry : sim.timer1_entry;
  const uint64_t deadline = sim.avr->cycle + CPU_HZ;
  while (sim.avr->pc != entry && sim.avr->cycle < deadline) avr_run(sim.avr);
  CHECK(sim.avr->pc == entry,"did not reach timer%d ISR for fault injection",delayed_edge?0:1);
  const unsigned before = sim.step_count[0];
  input_pin('E',6,0);
  // CPU is at the ISR's first instruction with I=0, so Timer2 cannot handle
  // this injected fault before the selected STEP fast path. Observe the real
  // SRAM alarm value instruction by instruction, before foreground reset.
  const uint64_t fault_deadline = sim.avr->cycle + 16000;
  while (sim.avr->data[sim.alarm_address] != 12 && sim.avr->cycle < fault_deadline)
    avr_run(sim.avr);
  CHECK(sim.avr->data[sim.alarm_address] == 12,
    "timer%d fast fault path failed to retain alarm12",delayed_edge?0:1);
  all_asleep(); run_us(3000);
  CHECK(sim.step_count[0] == before,"STEP edge escaped timer%d fault guard",delayed_edge?0:1);
  printf("PASS: deterministic FAULTn at Timer%d ISR entry cancels pending STEP, retains alarm12 before safety poll\n",delayed_edge?0:1);
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  if (argc != 2) { fprintf(stderr,"usage: %s firmware.elf\n",argv[0]); return 2; }
  boot(argv[1],0);
  verify_registers(); all_asleep();
  CHECK(strstr(command("$H\n",30),"error:"),"homing must be blocked");
  assert_ok("$X\n",30);
  const char *guard_commands[] = {"$0=9\n","$0=21\n","$0=10.5\n",
    "$2=1\n","$4=1\n","$5=1\n","$21=0\n","$22=1\n","$20=1\n","$32=1\n",
    "M3\n","M4\n","M7\n","M8\n","M62 P0\n","M64 P0\n","M67 E0 Q1\n",
    "$112=235\n"};
  for (unsigned i=0;i<sizeof(guard_commands)/sizeof(*guard_commands);++i)
    assert_error(guard_commands[i]);
  assert_ok("$N0=G21\n",350); assert_ok("$N1=G90\n",350);
  assert_ok("$I=EEPROMCHECK\n",350);
  const char *startup = command("$N\n",60);
  CHECK(strstr(startup,"$N0=G21") && strstr(startup,"$N1=G90"),"startup EEPROM rows overlap");
  CHECK(strstr(command("$I\n",60),"EEPROMCHECK"),"build information EEPROM row corrupted");
  assert_error("$N2=G21\n"); assert_error("$N-1=G21\n"); assert_error("$N0.5=G21\n");
  startup = command("$N\n",60);
  CHECK(strstr(startup,"$N0=G21") && strstr(startup,"$N1=G90"),"invalid index changed startup EEPROM");
  CHECK(strstr(command("$I\n",60),"EEPROMCHECK"),"invalid index changed build info EEPROM");
  CHECK(strstr(command("G38.2 X1 F10\n",30),"error:"),"unwired probing must be blocked");
  CHECK(strstr(command("G0 A1\n",30),"error:"),"reserve A must not be a logical axis");
  CHECK(strstr(command("G0 B1\n",30),"error:"),"reserve B must not be a logical axis");
  sim.verify_xy_synchronous = 1;
  assert_ok("G91 G1 X0.21 Y0.21 Z0.0125 F30\n",1200);
  command("?",30);
  CHECK(sim.step_count[0]==32 && sim.step_count[1]==32 && sim.step_count[2]==16,
    "XYZ pulse counts are %u/%u/%u, expected32/32/16",sim.step_count[0],sim.step_count[1],sim.step_count[2]);
  assert_ok("G91 G1 X-0.21 Y-0.21 Z-0.0125 F30\n",1200);
  CHECK(sim.step_count[0]==64 && sim.step_count[1]==64 && sim.step_count[2]==32,
    "reverse XYZ pulse counts wrong");
  sim.verify_xy_synchronous = 0;
  all_asleep();
  printf("PASS: five DRV register write/readback, alarm/admission, XYZ+reverse counts64/64/32; min STEP %.3fus DIR setup %.3fus\n",
    sim.min_pulse/16.0,sim.min_setup/16.0);

  // Approach the profile's 5000 physical steps/s ceiling, with continuous
  // UART status traffic. Runtime-only settings in this simulated EEPROM do
  // not change firmware defaults or authorize these rates on real hardware.
  assert_ok("$112=234\n",300); assert_ok("$122=1000\n",300);
  const unsigned z_start = sim.step_count[2];
  memset(sim.step_window,0,sizeof(sim.step_window));
  memset(sim.min_window_cycles,0,sizeof(sim.min_window_cycles));
  memset(sim.window_count,0,sizeof(sim.window_count));
  assert_ok("G91 G1 Z5 F234\n",30);
  for (int i=0;i<160;++i) command("?",10);
  CHECK(sim.step_count[2]-z_start==6400,"fast Z move did not produce exactly6400 steps");
  CHECK(sim.min_window_cycles[2],"missing fast Z timing window");
  const double z_rate = (double)CPU_HZ*64/sim.min_window_cycles[2];
  CHECK(z_rate > 4900 && z_rate < 5050,"fast Z rate %.2f outside expected4992Hz with ISR jitter",z_rate);
  all_asleep();
  printf("PASS: UART-stressed Z move6400 pulses at %.2f steps/s (fastest64-edge window), settings/M-word and EEPROM guards\n",z_rate);

  // A physical Z-max on a different AVR port must stop an in-progress move.
  assert_ok("G91 G1 X10 Y10 Z0.1 F100\n",150);
  input_pin('H',1,0);
  run_us(2000); all_asleep();
  const unsigned stopped = sim.step_count[0];
  run_us(600000); // Grbl deliberately waits 500 ms while reporting an alarm.
  CHECK(sim.step_count[0] == stopped,"steps continued after Z-max alarm");
  CHECK(strstr(sim.log,"ALARM:1"),"Z-max did not produce hard-limit alarm");
  command("\030",100); // Exit Grbl's critical hard-limit wait, still active.
  CHECK(strstr(command("$X\n",30),"error:"),"active Z-max must prevent unlock");
  input_pin('H',1,1); run_us(7000);
  assert_ok("$X\n",30);
  printf("PASS: PH1 Z-max alarm stops all five drivers; active contact blocks unlock\n");

  assert_ok("G91 G1 X10 F100\n",150);
  input_pin('E',6,0); run_us(2000); all_asleep();
  const unsigned fault_stopped = sim.step_count[0];
  run_us(600000);
  CHECK(sim.step_count[0] == fault_stopped,"steps continued after FAULTn");
  CHECK(strstr(sim.log,"ALARM:12"),"FAULTn did not produce driver alarm");
  input_pin('E',6,1);
  command("\030",700);
  CHECK(strstr(command("$X\n",30),"error:"),"driver fault latch cleared by soft reset");
  all_asleep();
  printf("PASS: shared FAULTn stops motion; software reset cannot clear sticky fault\n");
  avr_terminate(sim.avr);

  boot(argv[1],1);
  CHECK(strstr(command("$X\n",30),"error:"),"bad SPI readback admitted unlock");
  CHECK(strstr(command("G0 X1\n",30),"error:"),"bad SPI readback admitted motion");
  all_asleep();
  printf("PASS: intentionally corrupted DRV readback blocks motion and unlock\n");
  avr_terminate(sim.avr);
  inject_fault_at_isr(argv[1],0);
  inject_fault_at_isr(argv[1],1);
  boot(argv[1],0);
  assert_ok("$X\n",30);
  input_pin('E',6,0);
  run_us(1);
  input_pin('E',6,1); // Gone long before the next 1 kHz safety poll.
  run_us(700000);
  all_asleep();
  CHECK(strstr(sim.log,"ALARM:12"),"short FAULTn pulse was lost");
  command("\030",700);
  assert_error("$X\n");
  printf("PASS: INT6 latches a completed 1us FAULTn pulse and blocks unlock\n");
  avr_terminate(sim.avr);
  return 0;
}
