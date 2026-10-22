/* Real ATmega2560 digital timing/stop regression. Experimental mode patches
 * max_rate/acceleration in simulated SRAM only; it never raises the shipped
 * firmware ceiling or authorizes mechanical speeds. GPL-3.0+. */
#define RF2000_CNC_TEST_MAIN unused_cnc_suite
#include "rf2000_cnc_sim.c"

static uint16_t settings_address, position_address;
static double requested_rate;
static unsigned requested_pulse_us=10;
static int public_settings;
static float resolution[3];
static unsigned expected_pulses[3];
static unsigned pulse_divisor[3]={1,1,1};
static struct {
  unsigned count[3];
  int32_t signed_steps[3];
  uint64_t last_rise[3], last_fall[3], min_low[3], max_interval[3];
  uint64_t plateau_first[3], plateau_last[3], plateau_window[3][64];
  uint64_t min_window[3], max_window[3];
  unsigned plateau_count[3];
  uint8_t last_level[3];
} timing;

static uint16_t find_symbol(const char *elf,const char *name)
{
  elf_firmware_t f={0};
  CHECK(!elf_read_firmware(elf,&f),"cannot inspect ELF %s",elf);
  for (unsigned i=0;i<f.symbolcount;++i)
    if (!strcmp(f.symbol[i]->symbol,name)) return (uint16_t)(f.symbol[i]->addr-0x800000);
  CHECK(0,"ELF symbol %s absent",name); return 0;
}

static float read_float(uint16_t address)
{
  float value; memcpy(&value,sim.avr->data+address,sizeof(value)); return value;
}
static void write_float(uint16_t address,float value)
{
  memcpy(sim.avr->data+address,&value,sizeof(value));
}

static void speed_set(const char *line)
{
  const size_t start=sim.log_len;
  command(line,0);
  const uint64_t deadline=sim.avr->cycle+CPU_HZ/2;
  while (!strstr(sim.log+start,"ok\r\n") && !strstr(sim.log+start,"error:") &&
         sim.avr->cycle<deadline) run_us(1000);
  CHECK(strstr(sim.log+start,"ok\r\n") && !strstr(sim.log+start,"error:"),
    "public setting rejected: %s reply=%s",line,sim.log+start);
}

static void speed_step(struct avr_irq_t *irq,uint32_t value,void *opaque)
{
  (void)irq;
  const unsigned axis=(uintptr_t)opaque;
  const uint8_t level=value&1, previous=timing.last_level[axis];
  timing.last_level[axis]=level;
  if (level==previous) return;
  if (!level) { timing.last_fall[axis]=sim.avr->cycle; return; }
  const uint64_t now=sim.avr->cycle;
  if (timing.last_fall[axis]) {
    const uint64_t low=now-timing.last_fall[axis];
    if (low<timing.min_low[axis]) timing.min_low[axis]=low;
    CHECK(low>=32,"axis%u STEP low %.3fus shorter than2us",axis,low/16.0);
  }
  const char port=axis==2 ? 'L' : 'F';
  const uint8_t bit=axis==0 ? 1 : axis==1 ? 7 : 1;
  const int direction=gpio_level(port,bit) ? (axis==0 ? -1 : 1) : (axis==0 ? 1 : -1);
  timing.signed_steps[axis]+=direction;
  const unsigned count=++timing.count[axis], expected=expected_pulses[axis];
  if (count>=expected/4 && count<=expected*3/4 && expected) {
    if (!timing.plateau_first[axis]) timing.plateau_first[axis]=now;
    timing.plateau_last[axis]=now;
    const unsigned n=timing.plateau_count[axis]++, slot=n&63;
    if (timing.plateau_window[axis][slot]) {
      const uint64_t cycles=now-timing.plateau_window[axis][slot];
      if (cycles<timing.min_window[axis]) timing.min_window[axis]=cycles;
      if (cycles>timing.max_window[axis]) timing.max_window[axis]=cycles;
    }
    timing.plateau_window[axis][slot]=now;
    if (timing.last_rise[axis] && now-timing.last_rise[axis]>timing.max_interval[axis])
      timing.max_interval[axis]=now-timing.last_rise[axis];
  }
  timing.last_rise[axis]=now;
}

static void speed_boot(const char *elf)
{
  held_at_boot=-1; extra_peripheral_init=cnc_peripheral_init;
  boot(elf,0); extra_peripheral_init=NULL; run_us(300000);
  CHECK(strstr(sim.log,"RF2000 XYZ CNC"),"wrong profile");
  verify_registers(); all_asleep();
  CHECK(lcd.four_bit && lcd.data_bytes>=80,"live CNC LCD absent");
  settings_address=find_symbol(elf,"settings");
  position_address=find_symbol(elf,"sys_position");
  char setting[48];
  snprintf(setting,sizeof(setting),"$0=%u\n",requested_pulse_us);
  speed_set(setting);
  for (unsigned axis=0;axis<3;++axis) {
    pulse_divisor[axis]=1;
    resolution[axis]=read_float(settings_address+4*axis);
    CHECK(resolution[axis]>0 && resolution[axis]<10000,"bad SRAM resolution");
    if (public_settings) {
      snprintf(setting,sizeof(setting),"$%u=%.6f\n",110+axis,requested_rate*60/resolution[axis]);
      speed_set(setting);
      snprintf(setting,sizeof(setting),"$%u=2000\n",120+axis); speed_set(setting);
    } else {
      /* Test-only injection avoids modifying the production ceiling. CTRL
       * microsteps and all timer code remain intact. */
      write_float(settings_address+12+4*axis,(float)(requested_rate*60/resolution[axis]));
      write_float(settings_address+24+4*axis,2000.0f*60*60);
    }
  }
  assert_ok("$X\n",50);
  memset(&timing,0,sizeof(timing)); memset(expected_pulses,0,sizeof(expected_pulses));
  const uint8_t step_bits[3]={0,6,3};
  for (uintptr_t axis=0;axis<3;++axis) {
    timing.min_low[axis]=timing.min_window[axis]=UINT64_MAX;
    avr_irq_register_notify(pin_irq('F',step_bits[axis]),speed_step,(void *)axis);
  }
}

static void start_move(unsigned mask,int direction)
{
  const unsigned pulses=(unsigned)lround(requested_rate*0.4);
  char line[160]; unsigned used=(unsigned)snprintf(line,sizeof(line),"G21G91G1");
  double length_squared=0;
  for (unsigned axis=0;axis<3;++axis) {
    expected_pulses[axis]=(mask&(1u<<axis)) ? pulses/pulse_divisor[axis] : 0;
    if (mask&(1u<<axis)) {
      const double distance=expected_pulses[axis]/resolution[axis];
      length_squared+=distance*distance;
      used+=(unsigned)snprintf(line+used,sizeof(line)-used,"%c%.6f",'X'+axis,direction*distance);
    }
  }
  snprintf(line+used,sizeof(line)-used,"F%.5f\n",sqrt(length_squared)*150.0);
  const char *reply=command(line,0);
  CHECK(!strstr(reply,"error:"),"speed motion rejected: %s",reply);
}

static void speed_wait_steps(unsigned axis,unsigned minimum)
{
  const uint64_t deadline=sim.avr->cycle+CPU_HZ*2;
  while (timing.count[axis]<minimum && sim.avr->cycle<deadline) run_us(500);
  CHECK(timing.count[axis]>=minimum,"high-rate motion never reached step%u",minimum);
}

static void wait_idle(void)
{
  const uint64_t deadline=sim.avr->cycle+CPU_HZ*3;
  do {
    CHECK(!strstr(sim.log,"ALARM:"),"high-rate move alarmed: %s",sim.log);
    if (strstr(command("?",10),"<Idle")) return;
  } while (sim.avr->cycle<deadline);
  CHECK(0,"high-rate move did not return Idle");
}

static void check_timing(unsigned mask,const char *label)
{
  for (unsigned axis=0;axis<3;++axis) {
    CHECK(timing.count[axis]==expected_pulses[axis],"%s axis%u pulses%u expected%u",label,axis,timing.count[axis],expected_pulses[axis]);
    if (!(mask&(1u<<axis))) continue;
    CHECK(timing.plateau_count[axis]>64,"plateau too short");
    CHECK(sim.min_pulse>=requested_pulse_us*16,
      "STEP high %.3fus shorter thanconfigured%uus",sim.min_pulse/16.0,requested_pulse_us);
    const double mean=(timing.plateau_count[axis]-1)*(double)CPU_HZ/
      (timing.plateau_last[axis]-timing.plateau_first[axis]);
    const double fastest=64.0*CPU_HZ/timing.min_window[axis];
    const double slowest=64.0*CPU_HZ/timing.max_window[axis];
    const double axis_rate=requested_rate/pulse_divisor[axis];
    printf("MEASURE: %s axis%c rate requested%.2f mean%.2f fastest64%.2f slowest64%.2f maxinterval%.3fus minLOW%.3fus minHIGH%.3fus DIRsetup%.3fus pulses%u\n",
      label,'X'+axis,axis_rate,mean,fastest,slowest,timing.max_interval[axis]/16.0,
      timing.min_low[axis]/16.0,sim.min_pulse/16.0,sim.min_setup/16.0,timing.count[axis]);
    CHECK(mean>axis_rate*0.97 && mean<axis_rate*1.03,
      "%s axis%c sustained rate %.2f differs from requested%.2f",label,'X'+axis,mean,axis_rate);
    CHECK(slowest>axis_rate*0.92 && fastest<axis_rate*1.08,
      "%s axis%c 64-edge jitter outside8%% bounds",label,'X'+axis);
    int32_t stored; memcpy(&stored,sim.avr->data+position_address+4*axis,4);
    CHECK(stored==timing.signed_steps[axis],"stored position differs from physical signed pulses");
  }
  reserves_off();
}

static void test_timing(const char *elf,unsigned mask,int reverse,int unequal)
{
  speed_boot(elf); sim.verify_xy_synchronous=(mask&3)==3;
  if (unequal) { pulse_divisor[1]=2; pulse_divisor[2]=4; sim.verify_xy_synchronous=0; }
  const unsigned lcd_before=lcd.data_bytes;
  const size_t log_start=sim.log_len;
  const unsigned first_axis=(mask&1) ? 0 : (mask&2) ? 1 : 2;
  start_move(mask,1); speed_wait_steps(first_axis,1); wait_idle(); check_timing(mask,"forward");
  CHECK(lcd.data_bytes>lcd_before+40,"LCD stalled during high-rate motion");
  unsigned status_count=0; const char *cursor=sim.log+log_start;
  while ((cursor=strstr(cursor,"<Run"))) { ++status_count; ++cursor; }
  CHECK(status_count>=10,"UART status stalled during high-rate motion (%u reports)",status_count);
  if (reverse) {
    memset(&timing,0,sizeof(timing));
    for (unsigned a=0;a<3;++a) timing.min_low[a]=timing.min_window[a]=UINT64_MAX;
    start_move(mask,-1); speed_wait_steps(first_axis,1); wait_idle();
    for (unsigned a=0;a<3;++a) {
      CHECK(timing.count[a]==expected_pulses[a],"reverse pulse count mismatch");
      int32_t position; memcpy(&position,sim.avr->data+position_address+4*a,4);
      CHECK(position==0,"reverse did not restore actual position axis%u",a);
    }
    /* Counts are relative to this move; the position comparison above covers
     * the complete forward/backward move. */
    CHECK(sim.min_pulse>=requested_pulse_us*16,
      "reversed STEP pulse shorter thanconfigured%uus",requested_pulse_us);
    printf("PASS: reverse XYZ exact pulses and physical position returns to zero; minHIGH%.3fus DIRsetup%.3fus\n",
      sim.min_pulse/16.0,sim.min_setup/16.0);
  }
  printf("PASS: %s at%.0f requestedsteps/s with live LCD and100Hz status; exact physical pulses and position\n",mask==7 ? "XYZ" : mask==1 ? "X" : mask==2 ? "Y" : "Z",requested_rate);
  avr_terminate(sim.avr);
}

static void test_fault_loaded(const char *elf)
{
  speed_boot(elf); start_move(7,1);
  speed_wait_steps(0,expected_pulses[0]/3);
  const uint64_t trigger=sim.avr->cycle;
  input_pin('E',6,0); run_us(1); input_pin('E',6,1);
  run_us(1200); all_asleep();
  const unsigned stopped=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  CHECK(timing.last_rise[0]<=trigger || timing.last_rise[0]-trigger<16000,
    "driver fault took more than1ms to stop steps");
  run_us(600000);
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==stopped,"faulted high-rate XYZ restarted");
  CHECK(strstr(sim.log,"ALARM:12"),"specific sticky driver alarm12 lost");
  reserves_off();
  printf("PASS: completed1usFAULTpulse interrupts full-rateXYZ, all five asleep within1.2ms and no laterSTEP; alarm12\n");
  avr_terminate(sim.avr);
}

static void test_fault(const char *elf,int delayed)
{
  speed_boot(elf); start_move(1,-1);
  const uint32_t entry=delayed ? sim.timer0_compare_entry : sim.timer1_entry;
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (sim.avr->pc!=entry && sim.avr->cycle<deadline) avr_run(sim.avr);
  CHECK(sim.avr->pc==entry,"fault injection ISR not reached");
  const unsigned before=sim.step_count[0]; input_pin('E',6,0);
  const uint64_t stop_deadline=sim.avr->cycle+16000;
  while (sim.avr->data[sim.alarm_address]!=12 && sim.avr->cycle<stop_deadline) avr_run(sim.avr);
  CHECK(sim.avr->data[sim.alarm_address]==12,"specific driver fault missing");
  all_asleep(); run_us(5000);
  CHECK(sim.step_count[0]==before,"STEP escaped deterministic fault guard"); reserves_off();
  printf("PASS: FAULT at Timer%d entry cancels pending STEP, alarm12, all five drivers asleep\n",delayed?0:1);
  avr_terminate(sim.avr);
}

static void speed_test_stop(const char *elf,unsigned kind)
{
  speed_boot(elf); start_move(7,1); speed_wait_steps(0,expected_pulses[0]/3);
  const unsigned before=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  const uint64_t trigger=sim.avr->cycle;
  if (kind<4) {
    const char ports[4]={'C','C','C','H'}; const uint8_t bits[4]={2,0,6,1};
    input_pin(ports[kind],bits[kind],0);
  } else if (kind==4) command("\030",0);
  else key_level(KEY_STOP2,0);
  run_us(kind==5 ? 1200000 : 1500);
  all_asleep();
  const unsigned stopped=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  CHECK(stopped>=before,"step counter reversed");
  run_us(600000);
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==stopped,"motion restarted after stop");
  CHECK(strstr(command("?",30),"<Alarm"),"stop did not remain alarm locked");
  if (kind<4) CHECK(strstr(sim.log,"ALARM:1"),"hard limit alarm1 absent");
  reserves_off();
  printf("PASS: stopcase%u interrupts requested%.0fHz XYZ load; no later pulses, all drivers asleep (triggercycle%"PRIu64")\n",kind,requested_rate,trigger);
  avr_terminate(sim.avr);
}

static void test_soft_limits(const char *elf)
{
  speed_boot(elf);
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  memset(&machine,0,sizeof(machine));
  machine.position[0]=64; machine.position[1]=80; machine.position[2]=-100;
  machine.enabled=1;
  const uint8_t bits[3]={0,6,3};
  for (uintptr_t axis=0;axis<3;++axis)
    avr_irq_register_notify(pin_irq('F',bits[axis]),cnc_step,(void *)axis);
  const char *home=command("$H\n",4500);
  CHECK(strstr(home,"ok\r\n") && !strstr(home,"ALARM:"),"soft-limit fixture homing failed");
  assert_ok("$20=1\n",350);
  const unsigned before=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  CHECK(strstr(command("$J=G21G90X1F20\n",100),"error:15"),"jog outside homed machine envelope not rejected");
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==before,"rejected jog moved");
  const char *normal=command("G21G90G53G1X1F20\n",700);
  CHECK(strstr(normal,"ALARM:2"),"normal motion outside homed envelope lacks soft-limit alarm2");
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==before,"soft-limit violation emitted steps");
  all_asleep(); reserves_off();
  printf("PASS: full homing/ZMAX preserved; enabled soft limits reject out-of-envelope jog without motion and alarm2 normal move\n");
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<3 || argc>4) return 2;
  requested_rate=strtod(argv[2],NULL);
  if (requested_rate<1000 || requested_rate>60000) return 2;
  const char *pulse=getenv("RF2000_TEST_PULSE_US");
  if (pulse) requested_pulse_us=(unsigned)strtoul(pulse,NULL,10);
  if (requested_pulse_us<10 || requested_pulse_us>20) return 2;
  public_settings=getenv("RF2000_SPEED_PUBLIC_SETTINGS") &&
    strcmp(getenv("RF2000_SPEED_PUBLIC_SETTINGS"),"0");
  printf("MODE: %s; pulse%uus; acceleration2000mm/s^2 is a digital stress fixture, not a machine recommendation\n",
    public_settings ? "public UART rate/acceleration settings" : "EXPERIMENTAL SRAM rate/acceleration injection",requested_pulse_us);
  unsigned cases=0;
  #define CASE(name,statement) do { if (argc==3 || !strcmp(argv[3],name)) { statement; ++cases; } } while(0)
  CASE("x",test_timing(argv[1],1,0,0)); CASE("y",test_timing(argv[1],2,0,0));
  CASE("z",test_timing(argv[1],4,0,0)); CASE("xyz",test_timing(argv[1],7,1,0));
  CASE("unequal",test_timing(argv[1],7,1,1));
  CASE("fault-step",test_fault(argv[1],0)); CASE("fault-delay",test_fault(argv[1],1));
  CASE("fault-loaded",test_fault_loaded(argv[1]));
  CASE("limit-x",speed_test_stop(argv[1],0)); CASE("limit-y",speed_test_stop(argv[1],1));
  CASE("limit-zmin",speed_test_stop(argv[1],2)); CASE("limit-zmax",speed_test_stop(argv[1],3));
  CASE("reset",speed_test_stop(argv[1],4)); CASE("pause-reset",speed_test_stop(argv[1],5));
  CASE("soft-limits",test_soft_limits(argv[1]));
  #undef CASE
  if (!cases) return 2;
  printf("PASS: %u real-AVR speed cases; requested%.0fHz; %s; pulse%uus\n",cases,requested_rate,
    public_settings ? "public UART settings" : "test-only SRAM rate/acceleration injection",requested_pulse_us);
  return 0;
}
