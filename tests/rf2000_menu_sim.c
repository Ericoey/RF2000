/* Actual ATmega2560 panel/menu regression. Decode the physical HD44780 bus
 * and STEP/DIR pins; never read the firmware's private menu state. GPL-3.0+. */
#define RF2000_CNC_TEST_MAIN unused_cnc_suite
#include "rf2000_cnc_sim.c"
#include "sim_cycle_timers.h"

static unsigned menu_selection;
static void menu_boot(const char *elf)
{
  cnc_boot(elf); menu_selection=0;
}
static unsigned menu_steps(void)
{
  return sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
}
static void menu_set_selection(unsigned wanted)
{
  while (menu_selection!=wanted) { press(KEY_DOWN); menu_selection=(menu_selection+1)%8; }
}
static void menu_enter(unsigned wanted)
{
  press(KEY_OK); menu_set_selection(wanted); press(KEY_OK); run_us(220000);
}
static void menu_wait_idle(unsigned milliseconds)
{
  const uint64_t deadline=sim.avr->cycle+(uint64_t)milliseconds*16000;
  while (sim.avr->cycle<deadline) {
    if (strstr(command("?",20),"<Idle")) { run_us(220000); return; }
  }
  CHECK(0,"panel motion did not return to Idle: %s",sim.log);
}
static void menu_no_motion(unsigned before)
{
  CHECK(menu_steps()==before,"navigation/settings unexpectedly emitted steps"); reserves_off();
}
static void menu_reference_start(unsigned chosen)
{
  menu_enter(2);
  for (unsigned i=0;i<chosen;++i) press(KEY_DOWN);
  press(KEY_OK); run_us(220000);
  CHECK(display_has("referenzieren?"),"reference confirmation missing:\n%s",display_text());
  menu_no_motion(0);
  press(KEY_OK);
}

static void test_menu_navigation(const char *elf)
{
  menu_boot(elf);
  press(KEY_STOP1); run_us(220000);
  CHECK(strstr(command("?",30),"<Alarm"),"Play at startup unlocked/started machine");
  CHECK(display_has("GESPERRT"),"startup Play rejection absent:\n%s",display_text());
  press(KEY_OK); // Acknowledge error, never execute Freigeben.
  CHECK(strstr(command("?",30),"<Alarm"),"error acknowledgement unlocked machine");
  press(KEY_OK); run_us(220000);
  CHECK(display_has("CNC MENUE") && display_has("Freigeben"),"OK did not open main menu");
  press(KEY_LEFT); run_us(220000);
  CHECK(display_has("OK:Menue") && !display_has("CNC MENUE"),"Left did not return to status");
  menu_enter(0);
  CHECK(strstr(command("?",30),"<Idle"),"menu unlock failed");
  menu_enter(6);
  CHECK(display_has("JOG-VORSCHUB") && display_has("F:20"),"jog speed page missing");
  for (unsigned i=0;i<5;++i) press(KEY_E4);
  run_us(220000); CHECK(display_has("F:30"),"jog feed upper bound not30");
  for (unsigned i=0;i<7;++i) press(KEY_E3);
  run_us(220000); CHECK(display_has("F:10"),"jog feed lower bound not10");
  press(KEY_UP); run_us(220000); CHECK(display_has("F:20"),"Up did not increase jog feed");
  press(KEY_LEFT); menu_set_selection(4); press(KEY_RIGHT); run_us(220000);
  CHECK(display_has("L X0 Y0 Z-0 Z+0") && display_has("DRV"),"Right did not enter diagnostics");
  press(KEY_LEFT); press(KEY_LEFT); run_us(220000);
  CHECK(display_has("OK:Menue"),"two Back presses did not return to start");
  menu_no_motion(0); all_asleep();
  printf("PASS: startup Play cannot unlock; status/menu/back/Right navigation; diagnostics and jog-feed10..30 bounds never move\n");
  avr_terminate(sim.avr);
}

static void test_menu_jogs(const char *elf)
{
  menu_boot(elf); cnc_unlock(); menu_enter(1);
  CHECK(display_has("JOG BEREIT") && display_has("S0.10"),"jog screen/settings missing:\n%s",display_text());
  const enum panel_key keys[6]={KEY_RIGHT,KEY_LEFT,KEY_UP,KEY_DOWN,KEY_E1,KEY_E2};
  for (unsigned i=0;i<6;++i) {
    const unsigned axis=i/2, before=sim.step_count[axis];
    press(keys[i]); menu_wait_idle(1500);
    CHECK(sim.step_count[axis]-before==2048,"physical key%u wrong0.1mm pulse count",keys[i]);
    CHECK(machine.position[axis]==(i%2 ? (axis==0 ? 64 : axis==1 ? 80 : -100) :
      (axis==0 ? 2112 : axis==1 ? 2128 : 1948)),"physical direction of key%u incorrect",keys[i]);
  }
  const unsigned count=menu_steps();
  for (unsigned i=0;i<4;++i) press(KEY_E4);
  run_us(220000); CHECK(display_has("S0.50"),"plus did not clamp jog distance at0.50mm");
  for (unsigned i=0;i<5;++i) press(KEY_E3);
  run_us(220000); CHECK(display_has("S0.01"),"minus did not clamp jog distance at0.01mm");
  press(KEY_OK); run_us(220000); CHECK(display_has("CNC MENUE"),"jog OK did not return to menu");
  menu_no_motion(count); reserves_off();
  printf("PASS: physical cross X+/X-/Y+/Y- and Z+/Z- each emit2048 1/256 pulses per0.1mm, correct direction; +/- distance0.01..0.50 and OK never move\n");
  avr_terminate(sim.avr);
}

static void test_menu_zero(const char *elf)
{
  menu_boot(elf); cnc_unlock();
  assert_ok("G10L2P1X1.25Y2.5Z3.75\n",180);
  menu_enter(3); press(KEY_DOWN); press(KEY_DOWN); // Only Y.
  press(KEY_OK); run_us(220000);
  CHECK(display_has("Nur Y hier = 0"),"axis-specific work-zero confirmation wrong:\n%s",display_text());
  menu_no_motion(0);
  press(KEY_LEFT); run_us(220000);
  CHECK(display_has("ARBEITSNULL SETZEN"),"cancel zero did not return to submenu");
  CHECK(strstr(command("$#\n",80),"[G54:1.250,2.500,3.750]"),"cancelled zero changed coordinates");
  press(KEY_OK); run_us(220000); menu_no_motion(0); press(KEY_OK); run_us(300000);
  CHECK(strstr(command("$#\n",80),"[G54:1.250,0.000,3.750]"),"Y-zero changed another axis or failed");
  menu_no_motion(0);
  printf("PASS: independent Y work zero requires a separate confirmation; Back cancels; only G54 Y changes, XYZ never step\n");
  avr_terminate(sim.avr);
}

static void menu_home_observe(unsigned expected_mask,uint8_t *observed)
{
  const char *screen=display_text();
  if (expected_mask==2 && strstr(screen,"REF Y:")) {
    if (strstr(screen,"SUCHEN")) *observed|=1;
    if (strstr(screen,"RUECKZUG")) *observed|=2;
    if (strstr(screen,"FEINANFAHRT")) *observed|=4;
    if (strstr(screen,"FREIFAHREN")) *observed|=8;
  } else if (expected_mask==7) {
    if (strstr(screen,"REF Z:")) *observed|=1;
    if (strstr(screen,"REF X+Y:")) {
      CHECK(*observed&1,"LCD showed XY phase before Z phase"); *observed|=2;
    }
  }
}
static void menu_await_home(unsigned mask,uint8_t *observed)
{
  const uint64_t deadline=sim.avr->cycle+20*CPU_HZ;
  while (sim.avr->cycle<deadline) {
    run_us(10000); menu_home_observe(mask,observed);
    if (machine.contacts[1]==2 && machine.releases[1]==2 &&
      (mask==2 || (machine.contacts[0]==2 && machine.releases[0]==2 &&
                  machine.contacts[2]==2 && machine.releases[2]==2))) {
      if (strstr(command("?",20),"<Idle")) { run_us(220000); return; }
    }
  }
  CHECK(0,"home did not complete within20s:\n%s",display_text());
}
static void test_menu_home(const char *elf)
{
  menu_boot(elf);
  assert_ok("$27=0.5\n",350); assert_ok("$26=100\n",350);
  machine.position[0]=2048; machine.position[1]=2048; machine.position[2]=-1024;
  machine.enabled=1;
  menu_reference_start(2); // Only Y from local submenu.
  uint8_t observed=0; menu_await_home(2,&observed);
  CHECK(observed==15,"Y LCD missed actual seek/pull-off/locate/final-pull-off: mask%u",observed);
  CHECK(!sim.step_count[0] && !sim.step_count[2] && display_has("-Y-"),"Y-only homing moved/referenced another axis");
  reserves_off(); avr_terminate(sim.avr);
  menu_boot(elf);
  assert_ok("$27=0.5\n",350); assert_ok("$26=100\n",350);
  machine.position[0]=2048; machine.position[1]=2048; machine.position[2]=-1024;
  machine.enabled=1;
  command("$H\n",1); observed=0; menu_await_home(7,&observed);
  CHECK(observed==3 && machine.first_axis==3,"full home did not report Z then XY on physical LCD");
  for (unsigned axis=0;axis<3;++axis)
    CHECK(machine.contacts[axis]==2 && machine.releases[axis]==2 && machine.phases[axis]==4,
      "axis%u standard four-phase homing changed",axis);
  CHECK(display_has("XYZ"),"completed full home lacks XYZ reference"); reserves_off();
  printf("PASS: local Y-only submenu+confirmation reports every actual four-phase action; full $H LCD reports Z then X+Y; contacts/releases/reference masks correct\n");
  avr_terminate(sim.avr);
}

static void test_menu_pause_resume(const char *elf)
{
  menu_boot(elf); cnc_unlock();
  assert_ok("G91G1X2F60\n",60); run_us(250000);
  CHECK(display_has("CNC FAHRT X"),"start screen does not name active X block:\n%s",display_text());
  key_level(KEY_STOP2,0); run_us(1200); key_level(KEY_STOP2,1); run_us(25000);
  // The X axis needs about100ms to decelerate from1mm/s. A debounced Play
  // during that braking interval must be consumed, never deferred to resume.
  press(KEY_STOP1); run_us(250000);
  CHECK(strstr(command("?",30),"<Hold"),"physical Pause failed or early Play resumed during braking");
  unsigned held=sim.step_count[0]; run_us(200000); CHECK(sim.step_count[0]==held,"held job still steps");
  for (unsigned i=0;i<12;++i) press(KEY_E3);
  CHECK(strstr(command("?",30),"Ov:10,100,100"),"physical minus did not clamp real feed override to10%%");
  for (unsigned i=0;i<12;++i) press(KEY_E4);
  CHECK(strstr(command("?",30),"Ov:100,100,100"),"physical plus did not clamp real feed override to100%%");
  CHECK(sim.step_count[0]==held,"feed override keys moved a held job");
  press(KEY_STOP1); menu_wait_idle(4000);
  CHECK(sim.indexer_displacement[0]==-40960 && !sim.step_count[1] && !sim.step_count[2],
    "Play did not finish exactly the same2mm job in independent TI indexer units");
  reserves_off();
  printf("PASS: actual X block shown; physical II controlled feed hold; +/- real feed override10..100%% while stopped; Play resumes same40960-fine-unit job across MODE changes\n");
  avr_terminate(sim.avr);
}

static void test_menu_jog_cancel(const char *elf)
{
  menu_boot(elf); cnc_unlock();
  assert_ok("$J=G91G21Y5F60\n",60); run_us(250000);
  press(KEY_STOP2); menu_wait_idle(1500);
  const unsigned stopped=sim.step_count[1];
  CHECK(sim.indexer_displacement[1]>0 && sim.indexer_displacement[1]<102400,
    "physical Pause did not cancel partial5mm jog in independent TI indexer units");
  press(KEY_STOP1); run_us(500000);
  CHECK(strstr(command("?",30),"<Idle") && sim.step_count[1]==stopped,"Play restarted a cancelled jog");
  CHECK(!strstr(sim.log,"ALARM:"),"short jog Pause unnecessarily reset/alarmed machine");
  reserves_off();
  printf("PASS: physical II decelerates/cancels jog to Idle; Play never restarts cancelled motion; no reset/alarm\n");
  avr_terminate(sim.avr);
}

static uint8_t wake_pause_armed;
static avr_cycle_count_t menu_pause_in_wake(avr_t *avr,avr_cycle_count_t when,void *opaque)
{
  (void)avr; (void)when; (void)opaque;
  key_level(KEY_STOP2,0);
  return 0;
}
static void menu_wake_changed(struct avr_irq_t *irq,uint32_t value,void *opaque)
{
  (void)irq; (void)opaque;
  if (wake_pause_armed && (value&1)) {
    wake_pause_armed=0;
    avr_cycle_timer_register_usec(sim.avr,1000,menu_pause_in_wake,NULL);
  }
}
static void test_menu_resets(const char *elf)
{
  menu_boot(elf); cnc_unlock(); wake_pause_armed=1;
  avr_irq_register_notify(pin_irq('D',7),menu_wake_changed,NULL);
  command("G91G1X1F60\n",900);
  CHECK(!wake_pause_armed,"test never reached driver wake-up");
  CHECK(strstr(sim.log,"ALARM:3") && strstr(command("?",30),"<Alarm"),
    "Pause during25ms driver wake did not reset cleanly");
  menu_no_motion(0); all_asleep();
  key_level(KEY_STOP2,1); run_us(60000); cnc_unlock();
  assert_ok("$J=G91G21X0.1F20\n",40); menu_wait_idle(1500);
  CHECK(sim.step_count[0]==2048,"machine not reusable after a wake-up Pause/reset");
  reserves_off(); avr_terminate(sim.avr);
  menu_boot(elf); cnc_unlock();
  assert_ok("G91G1X2F60\n",60); run_us(100000);
  key_level(KEY_STOP2,0); run_us(1200000); all_asleep();
  CHECK(strstr(command("?",30),"<Alarm"),"holding Pause1s failed to reset");
  const unsigned count=menu_steps(); assert_error("$X\n"); assert_error("$J=G91G21Y1F20\n");
  menu_no_motion(count); key_level(KEY_STOP2,1); run_us(60000);
  avr_terminate(sim.avr);
  menu_boot(elf); cnc_unlock();
  assert_ok("G91G1X2F60\n",60); run_us(100000);
  key_level(KEY_STOP1,0); key_level(KEY_STOP2,0); run_us(1200); all_asleep();
  const unsigned chord_stopped=menu_steps(); run_us(800000);
  CHECK(strstr(command("?",30),"<Alarm") && menu_steps()==chord_stopped,"Play+Pause chord failed immediate reset");
  // Keep Play physically held, release only Pause, then admit a fresh UART
  // move. A second Pause edge must work despite the previous reset latch.
  key_level(KEY_STOP2,1); run_us(60000);
  assert_ok("$X\n",60); assert_ok("G91G1Y1F60\n",60); run_us(100000);
  CHECK(sim.step_count[1]>0,"held-Play latch regression never began its second job");
  key_level(KEY_STOP2,0); run_us(1200); all_asleep();
  const unsigned second_stop=menu_steps(); run_us(800000);
  CHECK(strstr(command("?",30),"<Alarm") && menu_steps()==second_stop,
    "fresh Pause edge was suppressed after a Play-held chord reset");
  reserves_off(); avr_terminate(sim.avr);
  held_at_boot=KEY_STOP2; extra_peripheral_init=cnc_peripheral_init;
  boot(elf,0); extra_peripheral_init=NULL; run_us(1200000);
  assert_error("$X\n"); assert_error("$J=G91G21Y1F20\n"); menu_no_motion(0); all_asleep();
  held_at_boot=-1; key_level(KEY_STOP2,1); run_us(60000);
  printf("PASS: holding II1s resets; Play+II chord stops within1ms; held Pause at runtime and power-up blocks UART unlock/jog with all five drivers asleep\n");
  avr_terminate(sim.avr);
}

static void test_menu_busy_alarm(const char *elf)
{
  menu_boot(elf); cnc_unlock(); menu_enter(1);
  press(KEY_RIGHT);
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (sim.step_count[0]<2046 && sim.avr->cycle<deadline) run_us(10);
  CHECK(sim.step_count[0]>=2046 && sim.step_count[0]<2048,"short X jog did not reach its last two pulses");
  key_level(KEY_UP,0); run_us(250000);
  CHECK(sim.step_count[0]==2048 && !sim.step_count[1],"busy-edge key queued Y after X ended");
  key_level(KEY_UP,1); run_us(60000); press(KEY_OK); press(KEY_LEFT);
  menu_enter(2); press(KEY_OK); run_us(220000);
  CHECK(display_has("XYZ referenzieren?"),"reference consent screen not present");
  assert_ok("$J=G91G21Z0.5F30\n",40); run_us(220000);
  CHECK(display_has("JOG") && !display_has("referenzieren?"),"stale menu confirmation hides a UART movement");
  input_pin('E',6,0); run_us(1); input_pin('E',6,1); run_us(1200); all_asleep();
  run_us(800000);
  CHECK(display_has("Alarm12") && !display_has("referenzieren?"),"driver alarm did not replace stale menu overlay:\n%s",display_text());
  const unsigned count=menu_steps(); press(KEY_OK); run_us(220000); menu_no_motion(count);
  CHECK(strstr(command("?",30),"<Alarm"),"acknowledgement unlocked sticky fault"); reserves_off();
  avr_terminate(sim.avr);
  menu_boot(elf); cnc_unlock(); menu_enter(3);
  CHECK(display_has("ARBEITSNULL SETZEN"),"idle submenu fixture not open");
  input_pin('E',6,0); run_us(1); input_pin('E',6,1); run_us(801200);
  CHECK(display_has("Alarm12") && !display_has("ARBEITSNULL SETZEN"),
    "new alarm did not dismiss an idle submenu:\n%s",display_text());
  all_asleep(); menu_no_motion(0);
  // Critical faults deliberately require reset before normal menu dispatch.
  // Physical Pause acknowledges that lock, without clearing the sticky fault.
  press(KEY_STOP2); run_us(800000); menu_selection=0;
  press(KEY_OK); menu_set_selection(4); press(KEY_OK); run_us(220000);
  CHECK(display_has("DRV F") && display_has("A12"),"diagnostics not reachable after fault alarm");
  menu_no_motion(0); avr_terminate(sim.avr);
  menu_boot(elf); cnc_unlock();
  input_pin('C',0,0); run_us(801200);
  CHECK(display_has("ALARM01"),"first idle hard limit alarm missing");
  press(KEY_STOP2); run_us(800000); menu_selection=0;
  input_pin('C',0,1); run_us(12000); cnc_unlock(); menu_enter(3);
  CHECK(display_has("ARBEITSNULL SETZEN"),"repeat-alarm fixture not browsing submenu");
  input_pin('C',0,0); run_us(801200);
  CHECK(display_has("ALARM01") && !display_has("ARBEITSNULL SETZEN"),
    "a second alarm of the same number did not dismiss the menu:\n%s",display_text());
  all_asleep(); menu_no_motion(0);
  printf("PASS: near-end busy key cannot queue motion; UART action replaces confirmation; new/repeated alarms replace active/idle menus; diagnostics remain reachable after reset\n");
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) return 2;
  unsigned selected=0;
  #define CASE(name,fn) do { if (argc==2 || !strcmp(argv[2],name)) { fn(argv[1]); ++selected; } } while (0)
  CASE("navigation",test_menu_navigation); CASE("jogs",test_menu_jogs);
  CASE("zero",test_menu_zero); CASE("home",test_menu_home);
  CASE("pause-resume",test_menu_pause_resume); CASE("jog-cancel",test_menu_jog_cancel);
  CASE("resets",test_menu_resets); CASE("busy-alarm",test_menu_busy_alarm);
  #undef CASE
  if (!selected) return 2;
  printf("PASS: %u actual-AVR menu/control cases\n",selected);
  return 0;
}
