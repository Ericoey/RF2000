/* Real-ELF regression for the opt-in RF2000 Y-only commissioning profile.
 * Reuse the electrical/DRV8711 model of the normal profile. Physical switch
 * position below is only a digital model, not validation of installed wiring.
 * GPL-3.0+. */
#include "sim_elf.h"
#include "avr_eeprom.h"
static void y_load_firmware(avr_t *avr, elf_firmware_t *firmware);
static void y_terminate(avr_t *avr);
#define avr_load_firmware y_load_firmware
#define avr_terminate y_terminate
#define main rf2000_general_regression_main
#include "rf2000_sim.c"
#undef main
#undef avr_load_firmware
#undef avr_terminate

static uint8_t eeprom_sentinel[4096];

static void y_load_firmware(avr_t *avr, elf_firmware_t *firmware)
{
  avr_load_firmware(avr,firmware);
  for (unsigned i=0;i<sizeof(eeprom_sentinel);++i)
    eeprom_sentinel[i] = (uint8_t)(i*73+19);
  avr_eeprom_desc_t eeprom = {.ee=NULL,.offset=0,.size=sizeof(eeprom_sentinel)};
  // simavr1.6 returns -1 even when this ioctl succeeds. Its pointer result
  // proves the EEPROM exists and avoids depending on that return-value bug.
  avr_ioctl(avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee,"cannot access simulated EEPROM before firmware starts");
  memcpy(eeprom.ee,eeprom_sentinel,sizeof(eeprom_sentinel));
}

static void y_terminate(avr_t *avr)
{
  avr_eeprom_desc_t eeprom = {.ee=NULL,.offset=0,.size=sizeof(eeprom_sentinel)};
  avr_ioctl(avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee,"cannot inspect simulated EEPROM");
  for (unsigned i=0;i<sizeof(eeprom_sentinel);++i)
    CHECK(eeprom.ee[i]==eeprom_sentinel[i],
      "Y profile modified EEPROM[%u]: %02x instead of%02x",i,eeprom.ee[i],eeprom_sentinel[i]);
  avr_terminate(avr);
}

enum switch_model { SWITCH_FOLLOWS_POSITION, SWITCH_MISSING, SWITCH_STUCK,
  SWITCH_MISSING_ON_LOCATE };
typedef struct {
  int direction;
  unsigned pulses;
  uint64_t samples[64], fastest_window;
} y_phase_t;
static struct {
  enum switch_model model;
  int position;
  uint8_t previous_step, switch_active;
  unsigned contacts, releases, phase_count;
  y_phase_t phases[8];
} y_model;

static void y_profile_pin(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq;
  watched_pin_t *pin = opaque;
  const uint8_t level = value & 1;
  if (!is_output(pin->port,pin->bit)) return;
  if ((pin->port == 'D' && pin->bit == 7) ||
      (pin->port == 'K' && pin->bit == 0) ||
      (pin->port == 'A' && pin->bit == 2) ||
      (pin->port == 'C' && pin->bit == 7))
    CHECK(!level,"Y profile woke non-Y driver P%c%u",pin->port,pin->bit);
  if (pin->port != 'F' || pin->bit != 6) return;
  const uint8_t previous = y_model.previous_step;
  y_model.previous_step = level;
  if (!level || previous == level) return;

  // Repetier-derived default: PF7=0 is a negative Y move toward Y-Min.
  const int direction = sim.pins['F'-'A'][7] ? 1 : -1;
  if (!y_model.phase_count ||
      y_model.phases[y_model.phase_count-1].direction != direction) {
    CHECK(y_model.phase_count < 8,"unexpected extra Y homing direction reversals");
    y_model.phases[y_model.phase_count++].direction = direction;
  }
  y_phase_t *phase = &y_model.phases[y_model.phase_count-1];
  const unsigned slot = phase->pulses++ & 63;
  if (phase->samples[slot]) {
    const uint64_t duration = sim.avr->cycle - phase->samples[slot];
    if (!phase->fastest_window || duration < phase->fastest_window)
      phase->fastest_window = duration;
  }
  phase->samples[slot] = sim.avr->cycle;
  y_model.position += direction;
  uint8_t active = y_model.position <= 0;
  if (y_model.model == SWITCH_MISSING) active = 0;
  if (y_model.model == SWITCH_STUCK && y_model.contacts) active = 1;
  if (y_model.model == SWITCH_MISSING_ON_LOCATE && y_model.releases) active = 0;
  if (active != y_model.switch_active) {
    if (active) ++y_model.contacts;
    else ++y_model.releases;
    y_model.switch_active = active;
    input_pin('C',0,!active);
  }
}

static void boot_y(const char *elf, enum switch_model model, int position)
{
  boot(elf,0);
  all_asleep();
  memset(&y_model,0,sizeof(y_model));
  y_model.model = model;
  y_model.position = position;
  const char *ports = "DFKAC";
  const uint8_t bits[] = {7,6,0,2,7};
  for (unsigned i=0;i<sizeof(bits);++i) {
    watched_pin_t *pin = &sim.watch[ports[i]-'A'][bits[i]];
    avr_irq_register_notify(pin_irq(ports[i],bits[i]),y_profile_pin,pin);
  }
  for (unsigned d=0;d<5;++d)
    CHECK(!!(sim.regs[d][0]&1)==(d==1),"Y profile must arm only DRV2, got DRV%u CTRL=%03x",d+1,sim.regs[d][0]);
  CHECK(strstr(command("$$\n",150),"$22=0\r\n"),"Y profile must start with session homing disabled");
  assert_ok("$22=1\n",30);
}

static const char *await_reply(size_t start, unsigned timeout_ms)
{
  const uint64_t deadline = sim.avr->cycle + (uint64_t)timeout_ms*16000;
  while (sim.avr->cycle < deadline &&
      !strstr(sim.log+start,"ok\r\n") &&
      !strstr(sim.log+start,"error:") && !strstr(sim.log+start,"ALARM:"))
    run_us(10000);
  CHECK(sim.avr->cycle < deadline,"Y homing did not complete within %u ms",timeout_ms);
  return sim.log+start;
}

static void no_non_y_steps(void)
{
  CHECK(!sim.step_count[0] && !sim.step_count[2] &&
    !sim.step_count[3] && !sim.step_count[4],
    "Y profile emitted non-Y steps: %u/%u/%u/%u",
    sim.step_count[0],sim.step_count[2],sim.step_count[3],sim.step_count[4]);
}

static void wait_for_y_steps(unsigned minimum)
{
  const uint64_t deadline = sim.avr->cycle + 2*CPU_HZ;
  while (sim.step_count[1] < minimum && sim.avr->cycle < deadline) run_us(1000);
  CHECK(sim.step_count[1] >= minimum,"Y approach did not start");
  no_non_y_steps();
}

static void stopped_after_alarm(const char *alarm)
{
  run_us(3000);
  all_asleep();
  const unsigned stopped = sim.step_count[1];
  run_us(650000);
  CHECK(sim.step_count[1] == stopped,"Y emitted steps after abort/alarm");
  no_non_y_steps();
  CHECK(strstr(sim.log,alarm),"missing expected %s",alarm);
}

static void test_complete_at(const char *elf, unsigned contact_steps)
{
  // Exercise contacts before, at and after the5mm chunk boundary (762
  // rounded steps), as well as one well beyond the first planning chunk.
  boot_y(elf,SWITCH_FOLLOWS_POSITION,(int)contact_steps);
  const char *blocked[] = {"$H\n","$HX\n","$HZ\n","$HA\n","$HB\n"};
  for (unsigned i=0;i<sizeof(blocked)/sizeof(*blocked);++i) assert_error(blocked[i]);
  assert_ok("$22=0\n",30);
  assert_error("$HY\n");
  assert_ok("$22=1\n",30);
  const char *frozen[] = {"$22=2\n","$23=0\n","$24=100\n","$25=200\n",
    "$27=10\n","$101=1\n","$111=1000\n","$3=0\n","$21=0\n","$N0=G0Y1\n"};
  for (unsigned i=0;i<sizeof(frozen)/sizeof(*frozen);++i) assert_error(frozen[i]);
  CHECK(!sim.step_count[1],"rejected homing command moved Y");
  all_asleep();
  const size_t start = sim.log_len;
  command("$HY\n",0);
  const char *reply = await_reply(start,20000);
  CHECK(strstr(reply,"ok\r\n") && !strstr(reply,"ALARM:") && !strstr(reply,"error:"),
    "Y home failed: %s",reply);
  CHECK(y_model.phase_count==4,"expected approach/pull-off/locate/pull-off, got %u phases",y_model.phase_count);
  for (unsigned p=0;p<4;++p) {
    const y_phase_t *phase = &y_model.phases[p];
    CHECK(phase->direction==(p%2 ? 1 : -1),"phase%u moved in wrong direction",p);
    CHECK(phase->pulses >= (p ? 150 : contact_steps) &&
        phase->pulses <= (p ? 155 : contact_steps+2),
      "phase%u has %u pulses outside expected travel",p,phase->pulses);
    CHECK(phase->fastest_window,"phase%u lacks speed measurements",p);
    const double rate = (double)CPU_HZ*64/phase->fastest_window;
    CHECK(rate > (p==2 ? 49 : 149) && rate < (p==2 ? 53 : 156),
      "phase%u rate %.2f steps/s differs from fixed20/60mm/min profile",p,rate);
  }
  CHECK(y_model.contacts==2 && y_model.releases==2,
    "expected two switch contacts/releases, got %u/%u",y_model.contacts,y_model.releases);
  CHECK(y_model.position >= 150 && y_model.position <= 155,
    "final switch clearance is %d steps instead of1mm",y_model.position);
  run_us(100000); all_asleep(); no_non_y_steps();
  CHECK(strstr(command("?",30),"<Idle"),"successful Y home did not return Idle");
  printf("PASS: only $HY accepted; Y-only first contact at%u steps/1mm pull-off/slow locate/final pull-off,2 contacts,2 releases; XYZAB idle safe\n",contact_steps);
  const char *motion_blocked[] = {"G91G1Y-0.1F20\n","G0X1\n","$J=G91G21X0.1F20\n",
    "$J=G91G21Z0.1F20\n","$J=G90G21Y0.1F20\n","$J=G91G20Y0.1F20\n",
    "$J=G91G21Y0.6F20\n","$J=G91G21Y0.1F31\n","$J=G91G21Y0.1F0\n"};
  for (unsigned i=0;i<sizeof(motion_blocked)/sizeof(*motion_blocked);++i)
    assert_error(motion_blocked[i]);
  const unsigned before_jog = sim.step_count[1];
  const char *jog_reply = command("$J=G91G21Y-0.1F20\n$J=G91G21Y-0.1F20\n",1000);
  CHECK(strstr(jog_reply,"ok\r\n") && strstr(jog_reply,"error:"),
    "pipelined Y jog must accept one move and reject the queued move: %s",jog_reply);
  CHECK(sim.step_count[1]-before_jog==15,"bounded -0.1mm Y jog did not produce15 steps");
  assert_ok("$J=G91G21Y0.1F20\n",700);
  CHECK(sim.step_count[1]-before_jog==30,"bounded +0.1mm Y jog did not return15 steps");
  all_asleep(); no_non_y_steps();
  printf("PASS: fixed settings and normal G-code blocked; bounded incremental metric Y jog emits15+15 steps and rejects queued/unsafe jogs\n");
  y_terminate(sim.avr);
}

static void test_continuous(const char *elf)
{
  boot_y(elf,SWITCH_MISSING,300);
  command("$HY\n",0);
  const uint64_t deadline = sim.avr->cycle + 15*CPU_HZ;
  while (sim.step_count[1] < 1600 && sim.avr->cycle < deadline) run_us(10000);
  CHECK(sim.step_count[1]>=1600,"continuous Y search stopped before1600steps");
  CHECK(y_model.phase_count==1,"continuous Y search unexpectedly reversed direction");
  CHECK(!strstr(sim.log,"ALARM:"),"continuous initial search produced an alarm before switch/abort");
  command("\030",0);
  stopped_after_alarm("ALARM:6");
  CHECK(strstr(command("$$\n",150),"$22=0\r\n"),"continuous-search abort failed to clear session enable");
  printf("PASS: no-switch initial search continues beyond2 planning chunks/1600steps; Ctrl-X stops all drivers and clears session enable\n");
  y_terminate(sim.avr);
}

static void test_missing_locate(const char *elf)
{
  boot_y(elf,SWITCH_MISSING_ON_LOCATE,150);
  const size_t start = sim.log_len;
  command("$HY\n",0);
  CHECK(strstr(await_reply(start,22000),"ALARM:9"),"missing second Y contact must produce approach alarm9");
  stopped_after_alarm("ALARM:9");
  CHECK(y_model.phase_count==3 && y_model.contacts==1 && y_model.releases==1,
    "missing locate contact did not stop in the second approach");
  CHECK(y_model.phases[2].pulses>=760 && y_model.phases[2].pulses<=764,
    "second locate search exceeded its5mm bound: %u steps",y_model.phases[2].pulses);
  printf("PASS: missing second contact remains bounded to5mm and reports ALARM:9\n");
  y_terminate(sim.avr);
}

static void test_stuck(const char *elf)
{
  boot_y(elf,SWITCH_STUCK,150);
  size_t start = sim.log_len;
  command("$HY\n",0);
  CHECK(strstr(await_reply(start,6000),"ALARM:8"),"stuck Y switch must produce pull-off alarm8");
  stopped_after_alarm("ALARM:8");
  CHECK(y_model.phase_count==2 && y_model.contacts==1 && !y_model.releases,
    "stuck switch proceeded beyond first pull-off");
  CHECK(y_model.phases[1].pulses>=150 && y_model.phases[1].pulses<=155,
    "stuck switch pull-off exceeded1mm");
  printf("PASS: switch stuck after contact stops after1mm pull-off with ALARM:8\n");
  y_terminate(sim.avr);
}

static void test_initially_active(const char *elf)
{
  boot_y(elf,SWITCH_MISSING,300);
  input_pin('C',0,0); run_us(10000);
  const size_t start = sim.log_len;
  command("$HY\n",0);
  const char *reply = await_reply(start,1000);
  CHECK(strstr(reply,"error:") || strstr(reply,"ALARM:"),"initially active Y switch was accepted");
  run_us(100000); all_asleep(); no_non_y_steps();
  CHECK(!sim.step_count[1],"initially active Y switch emitted %u steps",sim.step_count[1]);
  printf("PASS: already-active Y switch blocks homing without any STEP\n");
  y_terminate(sim.avr);
}

static void test_abort(const char *elf, char port, uint8_t bit, const char *alarm)
{
  boot_y(elf,SWITCH_MISSING,300);
  command("$HY\n",0);
  wait_for_y_steps(20);
  if (port) {
    input_pin(port,bit,0);
    if (port=='E') { run_us(1); input_pin(port,bit,1); }
  } else command("\030",0);
  stopped_after_alarm(alarm);
  if (!port) {
    CHECK(strstr(command("$$\n",150),"$22=0\r\n"),"Ctrl-X failed to clear session homing enable");
    assert_error("$HY\n");
    all_asleep();
  }
  printf("PASS: Y homing aborted by %s, all five drivers asleep, no continuing STEP\n",
    !port ? "Ctrl-X" : port=='E' ? "1us FAULTn pulse" : port=='H' ? "Z-Max" : bit==2 ? "X-Min" : "Z-Min");
  y_terminate(sim.avr);
}

int main(int argc, char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) { fprintf(stderr,"usage: %s y-homing-firmware.elf [case]\n",argv[0]); return 2; }
  unsigned selected = 0;
  #define RUN_CASE(name, ...) do { if (argc==2 || !strcmp(argv[2],name)) { \
    __VA_ARGS__; ++selected; } } while (0)
  RUN_CASE("complete",test_complete_at(argv[1],900));
  RUN_CASE("boundary761",test_complete_at(argv[1],761));
  RUN_CASE("boundary762",test_complete_at(argv[1],762));
  RUN_CASE("boundary763",test_complete_at(argv[1],763));
  RUN_CASE("continuous",test_continuous(argv[1]));
  RUN_CASE("locate-missing",test_missing_locate(argv[1]));
  RUN_CASE("stuck",test_stuck(argv[1]));
  RUN_CASE("active",test_initially_active(argv[1]));
  RUN_CASE("xlimit",test_abort(argv[1],'C',2,"ALARM:1"));
  RUN_CASE("zmin",test_abort(argv[1],'C',6,"ALARM:1"));
  RUN_CASE("zmax",test_abort(argv[1],'H',1,"ALARM:1"));
  RUN_CASE("fault",test_abort(argv[1],'E',6,"ALARM:12"));
  RUN_CASE("reset",test_abort(argv[1],0,0,"ALARM:6"));
  #undef RUN_CASE
  if (!selected) { fprintf(stderr,"unknown test case: %s\n",argv[2]); return 2; }
  printf("PASS: all4096 patterned EEPROM bytes preserved across %u selected cases\n",selected);
  return 0;
}
