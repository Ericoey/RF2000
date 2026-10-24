/* Actual AVR adaptive-resolution regression. The independent peripheral model
 * follows the TI next-valid-indexer rule, not the firmware's virtual counters.
 * Tests do not establish mechanical accuracy, current or hardware timing.
 * GPL-3.0-or-later. */
#define RF2000_TEST_XY_CTRL 0xe41
#define RF2000_TEST_Z_CTRL 0xe41
#define RF2000_CNC_TEST_MAIN unused_previous_cnc_suite
#include "rf2000_cnc_sim.c"

#define FINE_PER_MM 20480.0f
static uint16_t actual_position_address;
static uint8_t polarity_mask=6;
static int64_t coordinate_offset[3];
static size_t motion_log_start;
static struct {
  int64_t position[3];
  uint16_t phase[3];
  uint8_t enabled, active[3], previous_level[3];
  unsigned contacts[3], releases[3], phases[3];
  int previous_direction[3];
  uint64_t last_rise[3], last_fall[3], minimum_low;
} adaptive_machine;
static struct {
  uint8_t enabled;
  int64_t first_position[3], last_position[3];
  uint64_t first_cycle[3], last_cycle[3];
  unsigned samples[3];
} plateau;

static uint16_t symbol_address(const char *elf,const char *name)
{
  elf_firmware_t firmware={0};
  CHECK(!elf_read_firmware(elf,&firmware),"cannot read ELF for symbol lookup");
  for (unsigned i=0;i<firmware.symbolcount;++i)
    if (!strcmp(firmware.symbol[i]->symbol,name))
      return (uint16_t)(firmware.symbol[i]->addr-0x800000);
  CHECK(0,"missing ELF symbol %s",name); return 0;
}

static int32_t stored_position(unsigned axis)
{
  int32_t value;
  memcpy(&value,sim.avr->data+actual_position_address+axis*4,sizeof(value));
  return value;
}

static int64_t logical_hardware_position(unsigned axis)
{
  return (polarity_mask & (1u<<axis)) ? sim.indexer_displacement[axis] :
    -sim.indexer_displacement[axis];
}

static void anchor_coordinates(void)
{
  for (unsigned axis=0;axis<3;++axis)
    coordinate_offset[axis]=(int64_t)stored_position(axis)-logical_hardware_position(axis);
}

static void verify_actual_positions(void)
{
  for (unsigned axis=0;axis<3;++axis)
    CHECK(stored_position(axis)==logical_hardware_position(axis)+coordinate_offset[axis],
      "axis%c stored%d differs from independent weighted GPIO position%"PRId64,
      'X'+axis,stored_position(axis),logical_hardware_position(axis)+coordinate_offset[axis]);
  reserves_off();
}

static void adaptive_step(struct avr_irq_t *irq,uint32_t value,void *opaque)
{
  (void)irq;
  const unsigned axis=(uintptr_t)opaque;
  const uint8_t level=value&1, previous=adaptive_machine.previous_level[axis];
  adaptive_machine.previous_level[axis]=level;
  if (previous==level || !is_output('F',(uint8_t[]){0,6,3}[axis])) return;
  if (!level) { adaptive_machine.last_fall[axis]=sim.avr->cycle; return; }
  const unsigned mode=(sim.regs[axis][0]>>3)&15;
  CHECK(mode>=3 && mode<=8,"adaptive motion used unnecessary MODE%u",mode);
  if (adaptive_machine.last_fall[axis]) {
    const uint64_t low=sim.avr->cycle-adaptive_machine.last_fall[axis];
    if (low<adaptive_machine.minimum_low) adaptive_machine.minimum_low=low;
    CHECK(low>=32,"adaptive STEP low shorter than2us");
  }
  adaptive_machine.last_rise[axis]=sim.avr->cycle;
  /* simavr prepends IRQ listeners: this switch observer can run before the
   * shared indexer observer. Maintain its own TI phase rather than relying
   * on callback order or on the firmware's pending-step metadata. */
  const char port=axis==2 ? 'L' : 'F';
  const uint8_t bit=axis==0 ? 1 : axis==1 ? 7 : 1;
  const int positive=gpio_level(port,bit);
  const unsigned stride=256u>>mode;
  const unsigned remainder=adaptive_machine.phase[axis]&(stride-1u);
  const unsigned magnitude=positive ? stride-remainder : remainder ? remainder : stride;
  const int64_t physical_delta=positive ? (int)magnitude : -(int)magnitude;
  adaptive_machine.phase[axis]=(adaptive_machine.phase[axis]+(positive ? magnitude : 1024u-magnitude))&1023u;
  CHECK(physical_delta && llabs(physical_delta)<=32,
    "indexer delta%"PRId64" invalid for useful adaptive modes",physical_delta);
  const int direction=(physical_delta>0)==!!(polarity_mask&(1u<<axis)) ? 1 : -1;
  if (adaptive_machine.previous_direction[axis]!=direction) {
    adaptive_machine.previous_direction[axis]=direction;
    ++adaptive_machine.phases[axis];
  }
  adaptive_machine.position[axis] += direction*llabs(physical_delta);
  if (plateau.enabled) {
    const int64_t initial[3]={4096,5120,-6400};
    const int64_t position=adaptive_machine.position[axis]-initial[axis];
    if (position>=4*20480 && position<=8*20480) {
      if (!plateau.samples[axis]) {
        plateau.first_position[axis]=position;
        plateau.first_cycle[axis]=sim.avr->cycle;
      }
      plateau.last_position[axis]=position;
      plateau.last_cycle[axis]=sim.avr->cycle;
      ++plateau.samples[axis];
    }
  }
  if (!adaptive_machine.enabled) return;
  const uint8_t active=axis==2 ? adaptive_machine.position[axis]>=0 : adaptive_machine.position[axis]<=0;
  if (active!=adaptive_machine.active[axis]) {
    adaptive_machine.active[axis]=active;
    if (active) ++adaptive_machine.contacts[axis]; else ++adaptive_machine.releases[axis];
    if (axis==2) input_pin('H',1,!active);
    else input_pin('C',axis==0 ? 2 : 0,!active);
  }
}

static void adaptive_boot(const char *elf)
{
  held_at_boot=-1; extra_peripheral_init=cnc_peripheral_init;
  boot(elf,0); extra_peripheral_init=NULL; run_us(300000);
  CHECK(strstr(sim.log,"RF2000 XYZ CNC"),"wrong profile");
  verify_registers(); all_asleep();
  CHECK(lcd.four_bit && lcd.data_bytes>=80,"LCD absent");
  actual_position_address=symbol_address(elf,"sys_position");
  polarity_mask=6;
  memset(&adaptive_machine,0,sizeof(adaptive_machine));
  memset(&plateau,0,sizeof(plateau));
  adaptive_machine.minimum_low=UINT64_MAX;
  adaptive_machine.position[0]=4096; adaptive_machine.position[1]=5120;
  adaptive_machine.position[2]=-6400;
  const uint8_t bits[3]={0,6,3};
  for (uintptr_t axis=0;axis<3;++axis) {
    adaptive_machine.phase[axis]=sim.indexer_phase[axis];
    avr_irq_register_notify(pin_irq('F',bits[axis]),adaptive_step,(void *)axis);
  }
  anchor_coordinates();
  motion_log_start=sim.log_len;
}

static void adaptive_wait_idle(void)
{
  const uint64_t deadline=sim.avr->cycle+CPU_HZ*20;
  do {
    CHECK(!strstr(sim.log+motion_log_start,"ALARM:") &&
      !strstr(sim.log+motion_log_start,"error:"),"adaptive motion alarm/error");
    if (strstr(command("?",10),"<Idle")) {
      run_us(1000); verify_actual_positions(); return;
    }
  } while(sim.avr->cycle<deadline);
  CHECK(0,"adaptive motion did not finish");
}

static void adaptive_start(const char *line)
{
  motion_log_start=sim.log_len;
  assert_ok(line,40);
}

static void adaptive_move(const char *line)
{
  adaptive_start(line); adaptive_wait_idle();
}

static void configure_speed(void)
{
  for (unsigned axis=0;axis<3;++axis) {
    char line[40]; snprintf(line,sizeof(line),"$%u=1000\n",110+axis); assert_ok(line,350);
    /* Only a digital stress fixture. The machine defaults are not raised. */
    snprintf(line,sizeof(line),"$%u=1000\n",120+axis); assert_ok(line,350);
  }
  cnc_unlock();
}

static void expected_position(float x,float y,float z)
{
  const float target[3]={x,y,z};
  verify_actual_positions();
  for (unsigned axis=0;axis<3;++axis) {
    const int32_t expected=(int32_t)lroundf(target[axis]*FINE_PER_MM);
    CHECK(stored_position(axis)==expected,
      "axis%c final fine endpoint%d expected%d",'X'+axis,stored_position(axis),expected);
  }
}

static void test_adaptive_connect(const char *elf)
{
  adaptive_boot(elf);
  const char *identity=command("$I\n",100);
  CHECK(strstr(identity,"20261024-rf2000-cnc-adaptive-spi"),"adaptive identity absent");
  const char *identity_path=getenv("RF2000_HANDSHAKE_LOG");
  if (identity_path) {
    FILE *file=fopen(identity_path,"w");
    CHECK(file,"cannot open adaptive handshake transcript");
    CHECK(fputs(identity,file)>=0 && !fclose(file),"cannot write adaptive handshake transcript");
  }
  const char *micro=command("$MS\n",100);
  CHECK(strstr(micro,"[MS:VIRTUAL=256|MICRO=256|MAX=1000|PULSE_MAX=12000]"),
    "adaptive runtime microstep diagnostic absent");
  const char *settings=command("$$\n",350);
  CHECK(strstr(settings,"$100=20480.000") && strstr(settings,"$101=20480.000") &&
    strstr(settings,"$102=20480.000"),"virtual256-grid calibration absent");
  assert_error("$110=1001\n"); assert_error("$111=1001\n"); assert_error("$112=1001\n");
  assert_error("$24=1001\n"); assert_error("$25=1001\n");
  assert_error("$21=0\n"); assert_error("$5=1\n"); assert_error("G0A1\n");
  CHECK(!sim.step_count[0] && !sim.step_count[1] && !sim.step_count[2],"read/guard commands moved");
  reserves_off();
  puts("PASS: adaptive256-grid boot, MODE256XYZ/current/reserve profiles, identity, hard1000mm/min setting limits and unchanged safety guards");
  avr_terminate(sim.avr);
}

static void test_adaptive_modes(const char *elf)
{
  adaptive_boot(elf); configure_speed();
  const unsigned rates[]={20,50,100,200,400,800,1000};
  const unsigned modes[]={8,7,6,5,4,3,3};
  for (unsigned test=0;test<sizeof(rates)/sizeof(*rates);++test) {
    memset(sim.mode_pulses[0],0,sizeof(sim.mode_pulses[0]));
    char line[100]; const float distance=rates[test]/120.0f;
    snprintf(line,sizeof(line),"G21G90G1X%.6fF%u\n",distance,rates[test]);
    adaptive_move(line); expected_position(distance,0,0);
    CHECK(sim.mode_pulses[0][modes[test]],"F%u never produced expectedMODE%u",rates[test],modes[test]);
    printf("MEASURE: F%u MODE%u pulses%u finalfineposition%d\n",rates[test],modes[test],sim.mode_pulses[0][modes[test]],stored_position(0));
    adaptive_move("G21G90G1X0F1000\n"); expected_position(0,0,0);
  }
  for (unsigned mode=3;mode<=8;++mode)
    CHECK(sim.mode_changes[0],"no runtime MODE writes observed");
  CHECK(lcd.data_bytes>300,"LCD did not remain live while switching");
  puts("PASS: all six useful modes256/128/64/32/16/8 exercised through public feed changes, weighted physical endpoints and reverse return exact");
  avr_terminate(sim.avr);
}

static void test_adaptive_geometry(const char *elf)
{
  adaptive_boot(elf); configure_speed();
  adaptive_move("G21G90G1X0.00035F10\n"); expected_position(0.00035f,0,0);
  CHECK((sim.indexer_phase[0]&31)!=0,"unaligned phase fixture accidentally aligned");
  adaptive_move("G1X3.01735Y1.00315Z-0.12535F1000\n");
  expected_position(3.01735f,1.00315f,-0.12535f);
  adaptive_move("G1X0.00035Y0Z0F1000\n"); expected_position(0.00035f,0,0);
  adaptive_move("G1X0Y0Z0F500\n"); expected_position(0,0,0);
  adaptive_move("G2X0.5Y0.5I0.25J0.25F500\n"); expected_position(.5f,.5f,0);
  adaptive_move("G3X0Y0I-0.25J-0.25F500\n"); expected_position(0,0,0);
  for (unsigned i=0;i<12;++i) {
    char line[80]; const float x=(i&1) ? -.0017f : .0023f;
    snprintf(line,sizeof(line),"G1X%.6fY%.6fF1000\n",x,x*.5f);
    adaptive_move(line); expected_position(x,x*.5f,0);
  }
  adaptive_move("G1X0Y0Z0F1000\n"); expected_position(0,0,0);
  puts("PASS: unaligned electrical phase, unequal XYZ, real reversals, tiny endpoints and G2/G3 maintain exact final256-grid coordinates and independent weighted indexer positions");
  avr_terminate(sim.avr);
}

static void test_adaptive_speed(const char *elf,unsigned mask,unsigned pulse)
{
  adaptive_boot(elf); configure_speed();
  char line[96]; snprintf(line,sizeof(line),"$0=%u\n",pulse); assert_ok(line,350);
  const unsigned lcd_before=lcd.data_bytes;
  memset(sim.min_window_cycles,0,sizeof(sim.min_window_cycles));
  memset(sim.window_count,0,sizeof(sim.window_count));
  memset(sim.step_window,0,sizeof(sim.step_window));
  plateau.enabled=1;
  adaptive_move(mask==1 ? "G21G90G1X12F1000\n" : "G21G90G1X12Y12Z12F1000\n");
  plateau.enabled=0;
  expected_position(12,mask==7 ? 12 : 0,mask==7 ? 12 : 0);
  const double requested_axis_feed=mask==1 ? 1000 : 1000/sqrt(3.0);
  for (unsigned axis=0;axis<3;++axis) {
    if (!(mask&(1u<<axis))) continue;
    CHECK(plateau.samples[axis]>128,"adaptive plateau too short");
    const double feed=(plateau.last_position[axis]-plateau.first_position[axis])*
      (double)CPU_HZ*60/FINE_PER_MM/(plateau.last_cycle[axis]-plateau.first_cycle[axis]);
    const double fastest=64.0*CPU_HZ/sim.min_window_cycles[axis];
    CHECK(feed>requested_axis_feed*.97 && feed<requested_axis_feed*1.03,
      "axis%c measuredfeed%.3f expected%.3fmm/min",'X'+axis,feed,requested_axis_feed);
    CHECK(fastest<=12600,"physical pulse rate%.3f exceeds12k budget with5%% jitter margin",fastest);
    printf("MEASURE: adaptive axis%c pathF1000 actual%.3fmm/min fastest64physical%.3fHz pulse%uus minHIGH%.3fus minLOW%.3fus\n",
      'X'+axis,feed,fastest,pulse,sim.min_pulse/16.0,adaptive_machine.minimum_low/16.0);
  }
  CHECK(sim.min_pulse>=pulse*16,"configured%uus pulse was shortened",pulse);
  CHECK(lcd.data_bytes>lcd_before+40,"LCD stalled during adaptive plateau");
  printf("PASS: %s1000mm/min path atpulse%uus with liveLCD/status, weighted plateau and exact finalendpoint\n",mask==1 ? "X":"XYZ",pulse);
  avr_terminate(sim.avr);
}

static void test_adaptive_polarity(const char *elf)
{
  adaptive_boot(elf); configure_speed();
  assert_ok("$3=0\n",350); polarity_mask=0; anchor_coordinates();
  adaptive_move("G21G90G1X1.00315Y0.50175Z-0.10335F1000\n");
  expected_position(1.00315f,.50175f,-.10335f);
  adaptive_move("G1X0Y0Z0F1000\n"); expected_position(0,0,0);
  const uint16_t phases[3]={sim.indexer_phase[0],sim.indexer_phase[1],sim.indexer_phase[2]};
  const unsigned resets=sim.reset_count[0];
  command("\030",700);
  for (unsigned axis=0;axis<3;++axis)
    CHECK(sim.indexer_phase[axis]==phases[axis],"softreset reset electricalphase");
  CHECK(sim.reset_count[0]==resets,"softreset toggled sharedDRVRESET");
  anchor_coordinates(); cnc_unlock();
  adaptive_move("G21G90G1X0.00335Y-0.00175Z0.00235F20\n");
  expected_position(.00335f,-.00175f,.00235f);
  puts("PASS: alternateDIRpolarity, logical softreset and awake/sleep preserve electricalphase and weighted coordinate mapping");
  avr_terminate(sim.avr);
}

static void test_adaptive_home(const char *elf,int single)
{
  adaptive_boot(elf);
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  adaptive_machine.enabled=1;
  const char *line=single==0 ? "$HX\n" : single==1 ? "$HY\n" : single==2 ? "$HZ\n" : "$H\n";
  const size_t start=sim.log_len;
  command(line,0);
  const uint64_t deadline=sim.avr->cycle+CPU_HZ*12;
  while (!strstr(sim.log+start,"ok\r\n") && !strstr(sim.log+start,"ALARM:") && sim.avr->cycle<deadline) run_us(1000);
  CHECK(strstr(sim.log+start,"ok\r\n") && !strstr(sim.log+start,"ALARM:"),"adaptive homing failed");
  for (unsigned axis=0;axis<3;++axis) {
    if (single<0 || axis==(unsigned)single) {
      CHECK(adaptive_machine.contacts[axis]==2 && adaptive_machine.releases[axis]==2,
        "axis%c homing contacts/releases %u/%u",'X'+axis,adaptive_machine.contacts[axis],adaptive_machine.releases[axis]);
      CHECK(!adaptive_machine.active[axis],"homing left switch active");
    } else CHECK(!sim.step_count[axis],"singleaxis homing moved anotheraxis");
  }
  adaptive_machine.enabled=0; anchor_coordinates();
  /* A following normal move verifies phase after homing coordinate rebases. */
  cnc_unlock();
  const int32_t before=stored_position(1);
  adaptive_move("$J=G21G91Y0.001F20\n");
  CHECK(stored_position(1)==before+20 || stored_position(1)==before+21,"posthome fine jog endpoint wrong");
  reserves_off();
  printf("PASS: %s adaptive homing reaches two contacts/releases per selectedaxis; phase survives coordinate rebases; following weighted fine jog correct\n",single<0?"combinedXYZ":single==0?"X-only":single==1?"Y-only":"Z-only");
  avr_terminate(sim.avr);
}

/* A finite released-HIGH rise is separate from the driver's propagation
 * delay. Enable only after boot: this targets the new runtime MODE transport
 * without changing the already working startup read sequence. One microsecond
 * is an illustrative regression fixture, not a measurement of the RF2000 bus. */
static void test_adaptive_spi_slow_rise(const char *elf)
{
  adaptive_boot(elf);
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  sim.sdato_rise_delay_cycles = CPU_HZ/1000000UL;
  adaptive_machine.enabled = 1;
  const size_t start = sim.log_len;
  command("$H\n",0);
  const uint64_t deadline = sim.avr->cycle+CPU_HZ*12;
  while (!strstr(sim.log+start,"ok\r\n") &&
      !strstr(sim.log+start,"ALARM:") && sim.avr->cycle<deadline)
    run_us(1000);
  CHECK(strstr(sim.log+start,"ok\r\n") &&
      !strstr(sim.log+start,"ALARM:"),
    "1us released-HIGH SDATO rise broke first MODE32 homing readback");
  for (unsigned axis=0;axis<3;++axis) {
    CHECK(adaptive_machine.contacts[axis]==2 && adaptive_machine.releases[axis]==2,
      "slow-rise homing axis%c contacts/releases incomplete",'X'+axis);
    CHECK(sim.mode_pulses[axis][5],"slow-rise homing omitted MODE32");
    CHECK(!adaptive_machine.active[axis],"slow-rise homing left switch active");
  }
  adaptive_machine.enabled = 0;
  anchor_coordinates(); configure_speed();
  const int32_t before[3]={stored_position(0),stored_position(1),stored_position(2)};
  adaptive_move("G21G91G1X6F1000\n");
  CHECK(sim.mode_pulses[0][3],"slow-rise high-speed move never reached MODE8");
  CHECK(stored_position(0)==before[0]+6*20480 &&
      stored_position(1)==before[1] && stored_position(2)==before[2],
    "slow-rise mode transition changed the requested endpoint");
  adaptive_move("G21G91G1X-6F1000\n");
  for (unsigned axis=0;axis<3;++axis)
    CHECK(stored_position(axis)==before[axis],"slow-rise reverse endpoint differs");
  CHECK(sim.sdato_delayed_highs>50,"SDATO rise-delay fixture was not exercised");
  CHECK(strstr(command("$DRV\n",80),"READY=1|FAULT=0|READBACK=0"),
    "slow-rise readback test did not retain verified ready state");
  verify_actual_positions(); reserves_off();
  printf("PASS: 1us released-HIGH SDATO delay survives first MODE32 XYZ home and MODE8 F1000 forward/reverse; weighted positions exact, readback guards remain active (%u delayed rises)\n",
    sim.sdato_delayed_highs);
  avr_terminate(sim.avr);
}

static void start_loaded_move(void)
{
  adaptive_start("G21G91G1X10Y5Z2F1000\n");
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (sim.step_count[0]<100 && sim.avr->cycle<deadline) run_us(500);
  CHECK(sim.step_count[0]>=100,"loaded adaptive motion did not start");
}

static void test_adaptive_stop(const char *elf,unsigned kind)
{
  adaptive_boot(elf); configure_speed(); start_loaded_move();
  if (kind<4) {
    const char ports[4]={'C','C','C','H'}; const uint8_t bits[4]={2,0,6,1};
    input_pin(ports[kind],bits[kind],0);
  } else if (kind==4) { input_pin('E',6,0); run_us(1); input_pin('E',6,1); }
  else if (kind==5) command("\030",0);
  else key_level(KEY_STOP2,0);
  run_us(kind==6 ? 1200000 : 1500); all_asleep();
  const unsigned stopped=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  run_us(600000);
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==stopped,"adaptive stop executed later residual/correction motion");
  if (kind<4) CHECK(strstr(sim.log,"ALARM:1"),"hardlimit specific alarm absent");
  if (kind==4) CHECK(strstr(sim.log,"ALARM:12"),"driver specific alarm absent");
  reserves_off();
  printf("PASS: adaptive stopcase%u leaves all5asleep, retains specific alarm and never executes queued or residual correction steps\n",kind);
  avr_terminate(sim.avr);
}

static void test_adaptive_hold(const char *elf)
{
  adaptive_boot(elf); configure_speed(); start_loaded_move();
  command("!",400);
  CHECK(strstr(command("?",30),"<Hold"),"adaptive feedhold missing");
  verify_actual_positions();
  const unsigned held=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  run_us(200000);
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==held,"held adaptive move kept stepping");
  command("~",40); adaptive_wait_idle(); expected_position(10,5,2);
  puts("PASS: adaptive feedhold stops with true weighted positions and resumes the same exact fine-grid endpoint");
  avr_terminate(sim.avr);
}

static void test_adaptive_spi_fault(const char *elf,int corrupt)
{
  adaptive_boot(elf); configure_speed();
  const size_t start=sim.log_len;
  command("G21G91G1X10F1000\n",0);
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (!(sim.selected>=0 && sim.spi_bits>=8) && sim.avr->cycle<deadline) avr_run(sim.avr);
  CHECK(sim.selected>=0 && sim.spi_bits>=8,"runtime MODE SPI frame absent");
  const unsigned before=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  if (corrupt) sim.corrupt_readback=1;
  else { input_pin('E',6,0); run_us(1); input_pin('E',6,1); }
  run_us(700000); all_asleep();
  CHECK(strstr(sim.log+start,"ALARM:12"),"runtime MODE fault/readback did not retain driver alarm12");
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==before,"STEP escaped failed MODE publish barrier");
  command("\030",700); assert_error("$X\n"); all_asleep(); reserves_off();
  printf("PASS: %s during MODE transaction prevents STEP publication, sleeps all5 and remains sticky across CtrlX\n",corrupt?"readback mismatch":"completed1usFAULT");
  avr_terminate(sim.avr);
}

static void test_adaptive_spi_reset(const char *elf)
{
  adaptive_boot(elf); configure_speed();
  command("G21G91G1X10F1000\n",0);
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (!(sim.selected>=0 && sim.spi_bits>=8) && sim.avr->cycle<deadline) avr_run(sim.avr);
  CHECK(sim.selected>=0 && sim.spi_bits>=8,"runtimeMODEframe absent forreset");
  command("\030",700); all_asleep();
  verify_actual_positions();
  const unsigned stopped=sim.step_count[0]; run_us(100000);
  CHECK(sim.step_count[0]==stopped,"reset resumed partialMODE transactionmotion");
  const int32_t before=stored_position(0);
  cnc_unlock();
  adaptive_move("$J=G21G91X0.001F20\n");
  CHECK(stored_position(0)==before+20 || stored_position(0)==before+21,
    "postMODEreset resumed with incorrect physicalquantum");
  puts("PASS: CtrlX during MODE switching cancels motion; subsequent motion revalidates partial registers and retains exact weighted phase/position");
  avr_terminate(sim.avr);
}

static void test_adaptive_isr_fault(const char *elf,int delayed)
{
  adaptive_boot(elf); configure_speed();
  command("G21G91G1X10F1000\n",0);
  const uint32_t entry=delayed ? sim.timer0_compare_entry : sim.timer1_entry;
  const uint64_t deadline=sim.avr->cycle+CPU_HZ;
  while (sim.avr->pc!=entry && sim.avr->cycle<deadline) avr_run(sim.avr);
  CHECK(sim.avr->pc==entry,"adaptive timer ISR not reached");
  const unsigned before=sim.step_count[0]; input_pin('E',6,0);
  const uint64_t stop_deadline=sim.avr->cycle+16000;
  while (sim.avr->data[sim.alarm_address]!=12 && sim.avr->cycle<stop_deadline) avr_run(sim.avr);
  CHECK(sim.avr->data[sim.alarm_address]==12,"adaptive ISR fault lostalarm12");
  all_asleep(); run_us(5000);
  CHECK(sim.step_count[0]==before,"physicalSTEP escaped adaptiveISR faultguard");
  verify_actual_positions(); reserves_off();
  printf("PASS: adaptive Timer%d ISR-entryFAULT suppresses pending physicalpulse and virtualaccounting, alarm12\n",delayed?0:1);
  avr_terminate(sim.avr);
}

static void test_adaptive_soft_limits(const char *elf)
{
  adaptive_boot(elf);
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  adaptive_machine.enabled=1;
  const char *home=command("$H\n",6000);
  CHECK(strstr(home,"ok\r\n") && !strstr(home,"ALARM:"),"softlimit fixture home failed");
  adaptive_machine.enabled=0; anchor_coordinates();
  assert_ok("$20=1\n",350);
  const unsigned count=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  CHECK(strstr(command("$J=G21G90X1F20\n",100),"error:15"),"outofenvelope adaptivejog accepted");
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==count,"rejectedsoftlimitjog moved");
  CHECK(strstr(command("G21G90G53G1X1F20\n",700),"ALARM:2"),"adaptive normalsoftlimit alarm2 absent");
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==count,"normalsoftlimit violation moved");
  all_asleep(); reserves_off();
  puts("PASS: adaptive fullhoming and normal/jog softlimits preserve negativeGRBL machine envelope and reject withoutphysicalmovement");
  avr_terminate(sim.avr);
}

static void test_adaptive_recovery(const char *elf)
{
  adaptive_boot(elf); cnc_unlock();
  adaptive_machine.position[1]=-2048;
  adaptive_machine.active[1]=1; adaptive_machine.enabled=1;
  input_pin('C',0,0); run_us(700000);
  command("\030",700);
  anchor_coordinates();
  const int64_t before=logical_hardware_position(1);
  const unsigned x=sim.step_count[0],z=sim.step_count[2];
  const char *reply=command("$LR\n",7500);
  CHECK(strstr(reply,"ok\r\n") && !strstr(reply,"error:"),"adaptive boundedrecovery failed");
  CHECK(logical_hardware_position(1)-before==40960,"adaptive recovery not exactly2mm inweightedfineunits");
  CHECK(sim.step_count[0]==x && sim.step_count[2]==z,"adaptive recovery movedanotheraxis");
  CHECK(!adaptive_machine.active[1],"adaptive recovery leftselectedswitchactive");
  CHECK(strstr(command("?",30),"<Alarm"),"adaptive recovery automaticallyunlocked");
  all_asleep(); reserves_off();
  puts("PASS: adaptive bounded$LR uses fine256, exactly2mmY-only awayfromswitch, remainsAlarm and unreferenced");
  avr_terminate(sim.avr);
}

static void test_adaptive_migration(const char *elf)
{
  adaptive_boot(elf); cnc_unlock();
  assert_ok("G10L2P1X-1.25Y2.5Z-3\n",150);
  assert_ok("$N0=G21G90G54\n",350);
  assert_ok("$I=KEEP_ADAPTIVE\n",350);
  uint8_t image[4096], records[4096-512];
  avr_eeprom_desc_t eeprom={.ee=NULL,.offset=0,.size=sizeof(image)};
  avr_ioctl(sim.avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee && eeprom.ee[0]==76,"current adaptive record absent");
  memcpy(image,eeprom.ee,sizeof(image));
  memcpy(records,image+512,sizeof(records));
  cnc_v74_float(image,0,320.0f); cnc_v74_float(image,4,320.0f);
  cnc_v74_float(image,8,1280.0f);
  cnc_v74_float(image,12,100.0f); cnc_v74_float(image,16,100.0f);
  cnc_v74_float(image,20,150.0f);
  cnc_v74_checksum(image); image[0]=75;
  avr_terminate(sim.avr);

  eeprom_boot_image=image; adaptive_boot(elf); eeprom_boot_image=NULL;
  CHECK(strstr(sim.log,"EEPROM 75->76"),"actual AVR failed to announce75->76 migration");
  const char *reply=command("$$\n",350);
  CHECK(strstr(reply,"$100=20480.000") && strstr(reply,"$101=20480.000") &&
    strstr(reply,"$102=20480.000"),"actual75 physicalunits were not convertedto256virtualgrid");
  CHECK(strstr(reply,"$110=100.000") && strstr(reply,"$111=100.000") &&
    strstr(reply,"$112=150.000"),"migration unexpectedly raised retainedlowrates");
  CHECK(strstr(command("$#\n",100),"[G54:-1.250,2.500,-3.000]"),"migration erasedG54");
  CHECK(strstr(command("$N\n",100),"$N0=G21G90G54"),"migration erasedstartup");
  CHECK(strstr(command("$I\n",100),"KEEP_ADAPTIVE"),"migration erasedcustombuildtext");
  eeprom.ee=NULL; avr_ioctl(sim.avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee && eeprom.ee[0]==76 && !memcmp(eeprom.ee+512,records,sizeof(records)),
    "migration didnotcommit76 or alteredunrelatedEEPROMrecords");
  memcpy(image,eeprom.ee,sizeof(image)); avr_terminate(sim.avr);

  eeprom_boot_image=image; adaptive_boot(elf); eeprom_boot_image=NULL;
  CHECK(!strstr(sim.log,"75->76"),"already76 record scaledtwice");
  reply=command("$$\n",350);
  CHECK(strstr(reply,"$100=20480.000") && strstr(reply,"$101=20480.000") &&
    strstr(reply,"$102=20480.000"),"freshMCU changed76virtualcalibration");
  cnc_unlock();
  adaptive_move("$J=G21G91X1F60\n"); expected_position(1,0,0);
  adaptive_move("$J=G21G91Y1F60\n"); expected_position(1,1,0);
  adaptive_move("$J=G21G91Z1F60\n"); expected_position(1,1,1);
  for (unsigned axis=0;axis<3;++axis)
    CHECK(logical_hardware_position(axis)==20480,"migrated1mm%c jog not exactly20480weightedfineunits",'X'+axis);
  puts("PASS: actual AVR migrates EEPROM75 physical320/320/1280 to76 virtual20480XYZ once; retains100/100/150rates andG54/startup/buildrecords; freshMCU1mmXYZ weightedmoves exact");
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) return 2;
  unsigned cases=0;
  #define CASE(name,body) do { if(argc==2 || !strcmp(argv[2],name)) {body; ++cases;} } while(0)
  CASE("connect",test_adaptive_connect(argv[1]));
  CASE("modes",test_adaptive_modes(argv[1]));
  CASE("geometry",test_adaptive_geometry(argv[1]));
  CASE("speed-x",test_adaptive_speed(argv[1],1,10));
  CASE("speed-xyz",test_adaptive_speed(argv[1],7,10));
  CASE("speed-xyz-20us",test_adaptive_speed(argv[1],7,20));
  CASE("polarity",test_adaptive_polarity(argv[1]));
  CASE("home-x",test_adaptive_home(argv[1],0));
  CASE("home-y",test_adaptive_home(argv[1],1));
  CASE("home-z",test_adaptive_home(argv[1],2));
  CASE("home-xyz",test_adaptive_home(argv[1],-1));
  CASE("hold",test_adaptive_hold(argv[1]));
  CASE("limit-x",test_adaptive_stop(argv[1],0));
  CASE("limit-y",test_adaptive_stop(argv[1],1));
  CASE("limit-zmin",test_adaptive_stop(argv[1],2));
  CASE("limit-zmax",test_adaptive_stop(argv[1],3));
  CASE("fault-loaded",test_adaptive_stop(argv[1],4));
  CASE("reset",test_adaptive_stop(argv[1],5));
  CASE("pause-reset",test_adaptive_stop(argv[1],6));
  CASE("spi-slow-rise",test_adaptive_spi_slow_rise(argv[1]));
  CASE("spi-fault",test_adaptive_spi_fault(argv[1],0));
  CASE("spi-readback",test_adaptive_spi_fault(argv[1],1));
  CASE("spi-reset",test_adaptive_spi_reset(argv[1]));
  CASE("fault-step",test_adaptive_isr_fault(argv[1],0));
  CASE("fault-delay",test_adaptive_isr_fault(argv[1],1));
  CASE("soft-limits",test_adaptive_soft_limits(argv[1]));
  CASE("recovery",test_adaptive_recovery(argv[1]));
  CASE("migration",test_adaptive_migration(argv[1]));
  #undef CASE
  if (!cases) return 2;
  printf("PASS: %u actual-AVR adaptive-resolution cases; publicUART settings, independentTIphase model, no physicalhardware\n",cases);
  return 0;
}
