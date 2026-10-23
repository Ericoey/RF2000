/* Actual ATmega2560 homing regression: one XY switch arrives long before
 * the other. Observe physical STEP pins and the HD44780 bus. GPL-3.0+. */
#define RF2000_CNC_TEST_MAIN unused_cnc_suite
#include "rf2000_cnc_sim.c"

static void test_homing_wait(const char *elf, unsigned first)
{
  cnc_boot(elf);
  assert_ok("$27=0.1\n",350);
  assert_ok("$26=5\n",350);
  const unsigned other=first ^ 1u;
  machine.position[first]=13;
  machine.position[other]=6250;
  machine.enabled=1;
  const size_t log_start=sim.log_len;
  command("$H\n",1);
  const uint64_t contact_deadline=sim.avr->cycle+CPU_HZ*5;
  while (!machine.contacts[first] && sim.avr->cycle<contact_deadline) run_us(1000);
  CHECK(machine.contacts[first]==1 && !machine.contacts[other],
    "first %c switch was not reached before %c",'X'+first,'X'+other);
  CHECK(machine.first_axis==3 && machine.contacts[2]==2 && machine.releases[2]==2,
    "XY began before the complete Z reference cycle");

  // Give the foreground homing lock one complete Timer2 tick to react to
  // the physical switch. Then keep it held while the other axis travels.
  run_us(2000);
  const unsigned stopped_steps=sim.step_count[first], moving_steps=sim.step_count[other];
  run_us(500000);
  CHECK(machine.switch_active[first] && !machine.switch_active[other],
    "switch model did not retain the long one-axis waiting interval");
  CHECK(sim.step_count[first]==stopped_steps,
    "%c continued stepping into its active switch while waiting for %c",'X'+first,'X'+other);
  CHECK(sim.step_count[other]>moving_steps+125,
    "%c failed to continue seeking while %c waited",'X'+other,'X'+first);
  CHECK(display_has("REF X+Y: SUCHEN"),
    "LCD did not identify the still-running XY search:\n%s",display_text());
  CHECK(!strstr(sim.log+log_start,"ALARM:") && !strstr(sim.log+log_start,"error:"),
    "waiting for the second switch triggered a failure: %s",sim.log+log_start);
  printf("PASS: %c waits with its switch held for500ms and no further STEP; %c continues seeking; LCD stays REF X+Y: SUCHEN\n",
    'X'+first,'X'+other);

  const uint64_t finish_deadline=sim.avr->cycle+CPU_HZ*8;
  while (!strstr(sim.log+log_start,"ok\r\n") && sim.avr->cycle<finish_deadline) run_us(10000);
  CHECK(strstr(sim.log+log_start,"ok\r\n") &&
    !strstr(sim.log+log_start,"ALARM:") && !strstr(sim.log+log_start,"error:"),
    "home failed after both switches: %s",sim.log+log_start);
  for (unsigned axis=0;axis<3;++axis)
    CHECK(machine.contacts[axis]==2 && machine.releases[axis]==2 && machine.phases[axis]==4,
      "axis%u reference did not finish its four phases: contacts%u releases%u phases%u",
      axis,machine.contacts[axis],machine.releases[axis],machine.phases[axis]);
  CHECK(!machine.switch_active[0] && !machine.switch_active[1] && !machine.switch_active[2],
    "completed homing retained an active switch");
  run_us(250000);
  CHECK(display_has("XYZ") && display_has("BEREIT"),
    "completed homing LCD not referenced/ready:\n%s",display_text());
  CHECK(strstr(command("?",40),"<Idle"),"completed homing did not enter Idle");
  reserves_off();
  printf("PASS: %c-first full $H completes Z then XY seek/pull-off/locate/final-pull-off, all switches free and Idle XYZ\n",
    'X'+first);
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) return 2;
  unsigned cases=0;
  if (argc==2 || !strcmp(argv[2],"y-first")) { test_homing_wait(argv[1],1); ++cases; }
  if (argc==2 || !strcmp(argv[2],"x-first")) { test_homing_wait(argv[1],0); ++cases; }
  if (!cases) return 2;
  printf("PASS: %u actual-AVR staggered XY homing cases\n",cases);
  return 0;
}
