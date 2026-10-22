/* Actual ATmega2560 tests for the bounded, explicitly requested $LR escape.
 * Reuse the CNC EEPROM model, five DRV8711 channels and physical LCD decoder.
 * These checks do not prove switch polarity, mechanics or analog motor current.
 * GPL-3.0+. */
#define RF2000_CNC_TEST_MAIN unused_cnc_suite
#include "rf2000_cnc_sim.c"
#include "sim_cycle_timers.h"

static const char recovery_limit_port[4]={'C','C','C','H'};
static const uint8_t recovery_limit_bit[4]={2,0,6,1};
static const unsigned recovery_axis[4]={0,1,2,2};
static struct {
  unsigned sensor, pulses[3], releases, release_after, recontact_after;
  uint8_t last_step[3], active, bounce;
  int displacement[3];
  uint64_t first_step_cycle,last_step_cycle;
} escape;
static unsigned active_boot_sensor;

static void recovery_limit(unsigned sensor,uint8_t active)
{
  CHECK(sensor<4,"invalid simulated physical sensor");
  input_pin(recovery_limit_port[sensor],recovery_limit_bit[sensor],!active);
  if (sensor==escape.sensor) escape.active=active;
}

static avr_cycle_count_t recovery_bounce_close(avr_t *avr,avr_cycle_count_t when,void *opaque)
{
  (void)avr; (void)when; (void)opaque;
  recovery_limit(escape.sensor,1);
  return 0;
}

static void recovery_step(struct avr_irq_t *irq,uint32_t value,void *opaque)
{
  (void)irq;
  const unsigned axis=(uintptr_t)opaque;
  const uint8_t level=value&1, previous=escape.last_step[axis];
  escape.last_step[axis]=level;
  if (!level || previous) return;
  const char port=axis==2 ? 'L' : 'F';
  const uint8_t bit=axis==0 ? 1 : axis==1 ? 7 : 1;
  const int direction=gpio_level(port,bit) ? (axis==0 ? -1 : 1) : (axis==0 ? 1 : -1);
  ++escape.pulses[axis];
  if (!escape.first_step_cycle) escape.first_step_cycle=sim.avr->cycle;
  escape.last_step_cycle=sim.avr->cycle;
  escape.displacement[axis]+=direction;
  CHECK(axis==recovery_axis[escape.sensor],"$LR moved unselected axis%u",axis);
  CHECK(direction==(escape.sensor==3 ? -1 : 1),"$LR moved axis%u toward its physical switch",axis);
  if (escape.bounce && escape.pulses[axis]==20) {
    recovery_limit(escape.sensor,0);
    avr_cycle_timer_register_usec(sim.avr,2500,recovery_bounce_close,NULL);
  }
  if (escape.release_after && escape.pulses[axis]==escape.release_after) {
    recovery_limit(escape.sensor,0); ++escape.releases;
  }
  if (escape.recontact_after && escape.pulses[axis]==escape.recontact_after)
    recovery_limit(escape.sensor,1);
}

static void recovery_observe(unsigned sensor)
{
  memset(&escape,0,sizeof(escape)); escape.sensor=sensor;
  const uint8_t bits[3]={0,6,3};
  for (uintptr_t axis=0;axis<3;++axis)
    avr_irq_register_notify(pin_irq('F',bits[axis]),recovery_step,(void *)axis);
}

static avr_cycle_count_t activate_at_boot(avr_t *avr,avr_cycle_count_t when,void *opaque)
{
  (void)avr; (void)when; (void)opaque;
  recovery_limit(active_boot_sensor,1);
  return 0;
}

static void recovery_boot_peripheral(avr_t *avr)
{
  cnc_peripheral_init(avr);
  // Base boot() drives idle pins before the first AVR instruction. Assert the
  // physical switch ten microseconds later, well before GPIO/driver startup.
  avr_cycle_timer_register_usec(avr,10,activate_at_boot,NULL);
}

static void recovery_boot(const char *elf,unsigned sensor,uint8_t active_at_power_on)
{
  if (active_at_power_on) {
    held_at_boot=-1; active_boot_sensor=sensor;
    extra_peripheral_init=recovery_boot_peripheral;
    boot(elf,0); extra_peripheral_init=NULL;
    run_us(300000);
    CHECK(strstr(sim.log,"RF2000 XYZ CNC"),"wrong CNC startup");
    verify_registers(); all_asleep();
    CHECK(lcd.four_bit && display_has("Alarm"),"CNC LCD failed with an active boot sensor");
  } else cnc_boot(elf);
  CHECK(!sim.step_count[0] && !sim.step_count[1] && !sim.step_count[2],
    "an active boot/alarm sensor triggered unsolicited motion");
  recovery_observe(sensor);
  recovery_limit(sensor,1); run_us(12000);
}

static unsigned escape_steps(void)
{
  return escape.pulses[0]+escape.pulses[1]+escape.pulses[2];
}

static void assert_no_escape(void)
{
  CHECK(!escape_steps(),"rejected recovery emitted %u steps",escape_steps());
  reserves_off(); all_asleep();
}

static size_t recovery_start(void)
{
  const size_t start=sim.log_len;
  const char *reply=command("$LR\n",20);
  CHECK(!strstr(reply,"error:"),"valid $LR request rejected: %s",reply);
  return start;
}

static void wait_for_escape_steps(unsigned minimum)
{
  const uint64_t deadline=sim.avr->cycle+2*CPU_HZ;
  while (escape_steps()<minimum && sim.avr->cycle<deadline) run_us(1000);
  CHECK(escape_steps()>=minimum,"$LR failed to begin its bounded escape");
}

static const char *await_recovery(size_t start,unsigned timeout_ms)
{
  const uint64_t deadline=sim.avr->cycle+(uint64_t)timeout_ms*16000;
  const char *reply=sim.log+start;
  while (!strstr(reply,"RF2000 limit released; home again") &&
    !strstr(reply,"ALARM:") && !strstr(reply,"error:") && sim.avr->cycle<deadline)
    run_us(1000);
  CHECK(sim.avr->cycle<deadline,"$LR did not terminate within %ums",timeout_ms);
  run_us(800000); // Complete alarm UART output and one physical LCD frame.
  return sim.log+start;
}

static void assert_success(size_t start,unsigned expected)
{
  const char *reply=await_recovery(start,8000);
  CHECK(strstr(reply,"RF2000 limit released; home again") && !strstr(reply,"ALARM:"),
    "bounded release failed: %s",reply);
  CHECK(escape_steps()==expected,"$LR emitted %u rather than %u pulses",escape_steps(),expected);
  CHECK(abs(escape.displacement[recovery_axis[escape.sensor]])==(int)expected,
    "$LR reversed or lost physical displacement");
  const double motion_seconds=(double)(escape.last_step_cycle-escape.first_step_cycle)/CPU_HZ;
  CHECK(motion_seconds>5.95 && motion_seconds<6.2,
    "$LR2mm did not run at20mm/min: %.4fs",motion_seconds);
  CHECK(strstr(command("?",40),"<Alarm"),"successful release unlocked the machine");
  CHECK(display_has("---"),"successful release claimed a known reference:\n%s",display_text());
  reserves_off(); all_asleep();
  const unsigned frozen=escape_steps(); run_us(500000);
  CHECK(escape_steps()==frozen,"old planner motion resumed after release");
}

static void successful_sensor(const char *elf,unsigned sensor,uint8_t power_on,uint8_t bounce)
{
  recovery_boot(elf,sensor,power_on);
  // Withdrawal direction belongs to the identified physical MIN/MAX input,
  // never to the independently configurable homing-direction mask.
  if (sensor==0) assert_ok("$23=0\n",350);
  if (sensor==3) assert_ok("$23=7\n",350);
  escape.release_after=80; escape.bounce=bounce;
  const size_t start=recovery_start();
  assert_success(start,sensor<2 ? 640 : 2560);
  CHECK(escape.releases==1 && !escape.active,"selected physical sensor was not released");
  CHECK(sim.min_pulse>=160,"recovery pulse below10us");
  printf("PASS: $LR %s%s%s: exactly2mm away at20mm/min, no other axis/reserve, remains Alarm and unreferenced\n",
    sensor==0 ? "X-MIN" : sensor==1 ? "Y-MIN" : sensor==2 ? "Z-MIN" : "Z-MAX",
    power_on ? " active at power-on" : "",bounce ? " with sub-five-tick contact bounce" : "");
  avr_terminate(sim.avr);
}

static void test_release_x(const char *elf) { successful_sensor(elf,0,0,0); }
static void test_release_y(const char *elf) { successful_sensor(elf,1,0,0); }
static void test_release_zmin(const char *elf) { successful_sensor(elf,2,0,0); }
static void test_release_zmax(const char *elf) { successful_sensor(elf,3,0,0); }
static void test_release_active_boot(const char *elf) { successful_sensor(elf,1,1,0); }
static void test_release_bounce(const char *elf) { successful_sensor(elf,1,0,1); }

static void test_release_runtime_reset(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  assert_ok("G91G1Y10F60\n",60); assert_ok("G91G1X10F60\n",60);
  run_us(100000);
  CHECK(sim.step_count[1] && !sim.step_count[0],"queued pre-limit motion did not start on Y");
  recovery_limit(1,1); run_us(1200); all_asleep(); run_us(800000);
  CHECK(strstr(sim.log,"ALARM:1"),"running Y limit did not trigger alarm1");
  command("\030",800);
  CHECK(strstr(command("?",40),"<Alarm"),"reset with active switch lost alarm lock");
  const unsigned x_before=sim.step_count[0];
  const unsigned y_before=sim.step_count[1];
  recovery_observe(1); escape.active=1; escape.release_after=80;
  assert_error("$X\n"); assert_error("$J=G91G21Y1F20\n"); assert_error("$HY\n");
  assert_no_escape();
  const size_t start=recovery_start(); assert_success(start,640);
  CHECK(sim.step_count[0]==x_before && sim.step_count[1]==y_before+640,
    "recovery resumed or retained pre-limit planner blocks");
  printf("PASS: runtime hard limit stops queued job; Ctrl-X then $LR releases only Y; unlock/jog/home cannot bypass active switch; queued X never runs\n");
  avr_terminate(sim.avr);
}

static void test_release_rejected(const char *elf)
{
  cnc_boot(elf); recovery_observe(1);
  assert_error("$LR\n"); assert_error("$LRX\n"); assert_error("$LR=1\n"); assert_error("$LRY\n");
  assert_no_escape();
  recovery_limit(1,1); recovery_limit(0,1); run_us(10000);
  assert_error("$LR\n"); assert_no_escape();
  recovery_limit(0,0); run_us(10000);
  key_level(KEY_STOP2,0); run_us(1200);
  assert_error("$LR\n"); assert_no_escape();
  key_level(KEY_STOP2,1); run_us(60000);
  input_pin('E',6,0); run_us(1); input_pin('E',6,1); run_us(800000);
  command("\030",800);
  assert_error("$LR\n"); assert_no_escape();
  printf("PASS: $LR rejects no switch, malformed syntax, multiple physical switches, held STOP and sticky driver fault without motion\n");
  avr_terminate(sim.avr);
}

static void test_release_calibration_bounds(const char *elf)
{
  cnc_boot(elf); recovery_observe(1);
  // Existing GRBL settings admit these products at a very low maximum feed.
  // The fixed20mm/min escape must validate its own2mm pulse/rate budget.
  assert_ok("$111=0.0001\n",350);
  assert_ok("$101=2000000000\n",350);
  recovery_limit(1,1); run_us(12000);
  assert_error("$LR\n"); assert_no_escape();
  assert_ok("$101=1000000\n",350);
  assert_error("$LR\n"); assert_no_escape();
  assert_ok("$101=0.1\n",350);
  assert_error("$LR\n"); assert_no_escape();
  printf("PASS: $LR rejects a2mm signed-counter overflow, an excessive escape pulse budget, and sub-one-pulse calibration before any motor movement\n");
  avr_terminate(sim.avr);
}

static void assert_recovery_alarm(size_t start,unsigned alarm,unsigned maximum)
{
  const char *reply=await_recovery(start,8000);
  char expected[24]; snprintf(expected,sizeof(expected),"ALARM:%u",alarm);
  CHECK(strstr(reply,expected),"recovery expected %s: %s",expected,reply);
  CHECK(!strstr(reply,"RF2000 limit released; home again"),"failed recovery emitted success text");
  CHECK(escape_steps() && escape_steps()<=maximum,"recovery escaped unboundedly: %u steps",escape_steps());
  all_asleep(); reserves_off();
  const unsigned stopped=escape_steps(); run_us(500000);
  CHECK(escape_steps()==stopped,"recovery continued stepping after alarm%u",alarm);
}

static void test_release_stuck(const char *elf)
{
  recovery_boot(elf,1,0);
  const size_t start=recovery_start(); assert_recovery_alarm(start,8,640);
  CHECK(escape_steps()==640,"stuck switch did not stop exactly at2mm");
  printf("PASS: stuck selected Y switch stops exactly after2mm with alarm8 and all drivers asleep\n");
  avr_terminate(sim.avr);
}

static void test_release_recontact(const char *elf)
{
  recovery_boot(elf,1,0);
  escape.release_after=30; escape.recontact_after=90;
  const size_t start=recovery_start(); assert_recovery_alarm(start,1,92);
  CHECK(escape_steps()>=90,"recontact test did not reach its deliberate second contact");
  printf("PASS: selected switch is rearmed after stable release; recontact during escape stops within1ms as hard-limit alarm1\n");
  avr_terminate(sim.avr);
}

static void test_release_other_limit(const char *elf)
{
  recovery_boot(elf,1,0);
  const size_t start=recovery_start(); wait_for_escape_steps(40);
  const unsigned before=escape_steps();
  recovery_limit(3,1); run_us(1200); all_asleep();
  assert_recovery_alarm(start,1,before+2);
  printf("PASS: nonselected physical Z-MAX remains armed throughout Y release and stops all drivers within1ms\n");
  avr_terminate(sim.avr);
}

static void test_release_fault(const char *elf)
{
  recovery_boot(elf,1,0);
  const size_t start=recovery_start(); wait_for_escape_steps(40);
  const unsigned before=escape_steps();
  input_pin('E',6,0); run_us(1); input_pin('E',6,1); run_us(1200); all_asleep();
  assert_recovery_alarm(start,12,before+1);
  printf("PASS: one-microsecond driver FAULT pulse aborts $LR with sticky alarm12 and all five drivers asleep\n");
  avr_terminate(sim.avr);
}

static void interrupt_recovery(const char *elf,int control)
{
  recovery_boot(elf,1,0);
  const size_t start=recovery_start(); wait_for_escape_steps(40);
  const unsigned before=escape_steps();
  if (control<0) command(control==-1 ? "\030" : control==-2 ? "!" : "\205",2);
  else { key_level((enum panel_key)control,0); run_us(1200); }
  all_asleep();
  assert_recovery_alarm(start,6,before+2);
  printf("PASS: %s aborts bounded recovery with alarm6, no later pulses and all drivers asleep\n",
    control==-1 ? "Ctrl-X" : control==-2 ? "serial !" : control==-3 ? "UART0x85 jog-cancel" : control==KEY_LEFT ? "local LEFT/STOP" :
    control==KEY_STOP1 ? "local PH7 STOP" : "local PH2 STOP");
  avr_terminate(sim.avr);
}

static void test_release_reset(const char *elf) { interrupt_recovery(elf,-1); }
static void test_release_hold(const char *elf) { interrupt_recovery(elf,-2); }
static void test_release_jog_cancel(const char *elf) { interrupt_recovery(elf,-3); }
static void ignored_recovery_key(const char *elf,enum panel_key key)
{
  recovery_boot(elf,1,0); escape.release_after=80;
  const size_t start=recovery_start(); wait_for_escape_steps(40);
  press(key);
  assert_success(start,640);
  CHECK(!escape.active,"ignored navigation/play prevented the switch release");
  printf("PASS: %s cannot reset, resume, queue motion or change a running withdrawal; exactly2mm Y-only escape succeeds\n",
    key==KEY_LEFT ? "local LEFT navigation" : "local PH7 Play");
  avr_terminate(sim.avr);
}
static void test_release_local_left(const char *elf) { ignored_recovery_key(elf,KEY_LEFT); }
static void test_release_local_stop1(const char *elf) { ignored_recovery_key(elf,KEY_STOP1); }
static void test_release_local_stop2(const char *elf) { interrupt_recovery(elf,KEY_STOP2); }

static void test_release_panel_confirm(const char *elf)
{
  recovery_boot(elf,1,0); escape.release_after=80;
  press(KEY_OK); // Startup status -> menu, never unlock.
  for (unsigned i=0;i<5;++i) press(KEY_DOWN);
  run_us(250000);
  CHECK(display_has("Endschalter frei"),"front-panel release menu missing:\n%s",display_text());
  assert_no_escape();
  press(KEY_OK); run_us(250000);
  CHECK(display_has("Endschalter frei?"),"first OK did not show release confirmation:\n%s",display_text());
  assert_no_escape();
  // Leaving the menu cancels consent; returning must ask again.
  press(KEY_LEFT); press(KEY_OK); run_us(250000);
  CHECK(display_has("Endschalter frei?"),"cancelled release confirmation was not reset");
  assert_no_escape();
  const size_t start=sim.log_len;
  press(KEY_OK); run_us(200000);
  CHECK(display_has("RUECKZUG"),"active withdrawal phase not visible:\n%s",display_text());
  assert_success(start,640);
  printf("PASS: LCD Endschalter frei menu requires two distinct OK presses, cancellation clears consent, live RUECKZUG then Alarm without reference\n");
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) return 2;
  unsigned selected=0;
  #define CASE(name,fn) do { if (argc==2 || !strcmp(argv[2],name)) { fn(argv[1]); ++selected; } } while (0)
  CASE("deny",test_release_rejected); CASE("jog-cancel",test_release_jog_cancel);
  CASE("calibration-bounds",test_release_calibration_bounds);
  CASE("panel-confirm",test_release_panel_confirm);
  CASE("x-min",test_release_x); CASE("y-min",test_release_y);
  CASE("z-min",test_release_zmin); CASE("z-max",test_release_zmax);
  CASE("active-boot",test_release_active_boot); CASE("bounce",test_release_bounce);
  CASE("runtime-reset",test_release_runtime_reset);
  CASE("stuck",test_release_stuck); CASE("recontact",test_release_recontact);
  CASE("other-limit",test_release_other_limit); CASE("fault",test_release_fault);
  CASE("reset",test_release_reset); CASE("feed-hold",test_release_hold);
  CASE("local-left",test_release_local_left); CASE("local-stop1",test_release_local_stop1);
  CASE("local-stop2",test_release_local_stop2);
  #undef CASE
  if (!selected) return 2;
  printf("PASS: %u real-AVR limit-release cases\n",selected);
  return 0;
}
