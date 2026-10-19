/* Real-ELF tests for the RF2000 20x4 LCD and local commissioning keys.
 * Observe the actual HD44780 four-bit bus, not firmware framebuffer memory.
 * Reuse the five-DRV8711 and GPIO model of the standard profile. GPL-3.0+. */
#include "sim_elf.h"
#include "avr_eeprom.h"
#include <ctype.h>
static void panel_load_firmware(avr_t *avr, elf_firmware_t *firmware);
#define avr_load_firmware panel_load_firmware
#define main rf2000_general_regression_main
#include "rf2000_sim.c"
#undef main
#undef avr_load_firmware

enum panel_key { KEY_OK, KEY_DOWN, KEY_RIGHT, KEY_LEFT, KEY_UP,
  KEY_E1, KEY_E2, KEY_E3, KEY_E4, KEY_STOP1, KEY_STOP2, KEY_COUNT };
static const char key_port[KEY_COUNT] = {'G','B','H','B','L','J','J','J','J','H','H'};
static const uint8_t key_bit[KEY_COUNT] = {5,7,0,5,7,2,4,5,6,7,2};
static int held_at_boot = -1;
static uint8_t retained_eeprom[4096];
static struct {
  uint8_t four_bit, high_pending, high_nibble, high_rs, address;
  uint8_t ddram[128];
  unsigned raw_init, function_sets, display_on, entry_modes, data_bytes;
  uint64_t enable_rise;
  uint64_t shortest_enable;
  uint64_t last_byte_cycle;
} lcd;

static uint8_t gpio_level(char port, uint8_t bit)
{
  avr_ioport_state_t state = {0};
  avr_ioctl(sim.avr,AVR_IOCTL_IOPORT_GETSTATE(port),&state);
  return (state.port >> bit) & 1;
}

static void lcd_enable_changed(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq; (void)opaque;
  if (!is_output('K',3)) return;
  if (value & 1) { lcd.enable_rise = sim.avr->cycle; return; }
  if (!lcd.enable_rise) return;
  const uint64_t width = sim.avr->cycle-lcd.enable_rise;
  if (width < lcd.shortest_enable) lcd.shortest_enable = width;
  CHECK(width >= 8,"LCD E pulse shorter than450ns: %.3fus",width/16.0);
  lcd.enable_rise = 0;
  CHECK(is_output('K',1) && is_output('F',5) && is_output('K',2) &&
    is_output('A',1) && is_output('J',3),"LCD bus pin configured as input");
  const uint8_t rs = gpio_level('K',1);
  const uint8_t nibble = gpio_level('F',5) | (gpio_level('K',2)<<1) |
    (gpio_level('A',1)<<2) | (gpio_level('J',3)<<3);
  if (!lcd.four_bit) {
    CHECK(!rs,"LCD entered data mode before four-bit initialization");
    if (nibble==3) ++lcd.raw_init;
    else if (nibble==2) {
      CHECK(lcd.raw_init>=3,"LCD entered four-bit mode without three reset nibbles");
      lcd.four_bit = 1;
    } else CHECK(0,"unexpected LCD initialization nibble %x",nibble);
    return;
  }
  if (!lcd.high_pending) {
    if (lcd.last_byte_cycle) {
      const uint64_t spacing=sim.avr->cycle-lcd.last_byte_cycle;
      CHECK(spacing>=5120,"LCD byte spacing shorter than320us: %.3fus",spacing/16.0);
    }
    lcd.high_pending=1; lcd.high_nibble=nibble; lcd.high_rs=rs; return;
  }
  CHECK(rs==lcd.high_rs,"LCD RS changed between nibbles");
  const uint8_t byte = (lcd.high_nibble<<4)|nibble;
  lcd.high_pending=0;
  lcd.last_byte_cycle=sim.avr->cycle;
  if (rs) {
    lcd.ddram[lcd.address++ & 127]=byte; ++lcd.data_bytes;
  } else if (byte & 0x80) lcd.address=byte&127;
  else if (byte==1) { memset(lcd.ddram,' ',sizeof(lcd.ddram)); lcd.address=0; }
  else if (byte==2) lcd.address=0;
  else if ((byte&0xf0)==0x20) { CHECK(byte==0x28,"LCD function-set %02x is not20x4 four-bit mode",byte); ++lcd.function_sets; }
  else if ((byte&0xf8)==0x08) { if (byte&4) ++lcd.display_on; }
  else if ((byte&0xfc)==0x04) { CHECK(byte==6,"LCD must use increment/no-shift entry mode"); ++lcd.entry_modes; }
}

static const char *display_text(void)
{
  static char text[85];
  const uint8_t offset[4] = {0,0x40,0x14,0x54};
  for (unsigned row=0;row<4;++row) {
    for (unsigned col=0;col<20;++col) text[row*21+col]=(char)lcd.ddram[offset[row]+col];
    text[row*21+20]='\n';
  }
  text[84]=0;
  return text;
}

static int display_has(const char *needle)
{
  char upper[85];
  const char *text=display_text();
  for (unsigned i=0;i<sizeof(upper);++i) upper[i]=(char)toupper((unsigned char)text[i]);
  char target[48];
  CHECK(strlen(needle)<sizeof(target),"display search too long");
  unsigned i=0;
  do { target[i]=(char)toupper((unsigned char)needle[i]); } while (needle[i++]);
  return strstr(upper,target)!=NULL;
}

static void key_level(enum panel_key key, uint8_t released)
{
  input_pin(key_port[key],key_bit[key],released);
}

static void panel_load_firmware(avr_t *avr, elf_firmware_t *firmware)
{
  avr_load_firmware(avr,firmware);
  memset(&lcd,0,sizeof(lcd));
  memset(lcd.ddram,' ',sizeof(lcd.ddram));
  lcd.shortest_enable=UINT64_MAX;
  avr_irq_register_notify(pin_irq('K',3),lcd_enable_changed,NULL);
  for (unsigned key=0;key<KEY_COUNT;++key) key_level((enum panel_key)key,(int)key!=held_at_boot);
  for (unsigned i=0;i<sizeof(retained_eeprom);++i) retained_eeprom[i]=(uint8_t)(i*73+19);
  avr_eeprom_desc_t eeprom = {.ee=NULL,.offset=0,.size=sizeof(retained_eeprom)};
  avr_ioctl(avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee,"cannot inspect simulated EEPROM");
  memcpy(eeprom.ee,retained_eeprom,sizeof(retained_eeprom));
}

static void panel_terminate(void)
{
  avr_eeprom_desc_t eeprom = {.ee=NULL,.offset=0,.size=sizeof(retained_eeprom)};
  avr_ioctl(sim.avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee && !memcmp(eeprom.ee,retained_eeprom,sizeof(retained_eeprom)),
    "local panel altered the retained4096 EEPROM bytes");
  avr_terminate(sim.avr);
}

static void no_other_steps(void)
{
  CHECK(!sim.step_count[0] && !sim.step_count[2] && !sim.step_count[3] && !sim.step_count[4],
    "panel emitted non-Y steps: %u/%u/%u/%u",sim.step_count[0],sim.step_count[2],sim.step_count[3],sim.step_count[4]);
}

static struct {
  int position;
  uint8_t enabled, level, previous_step;
  unsigned contacts, releases, phases, phase_steps[8], wakes;
  int phase_direction[8];
} switch_model;

static void panel_y_sleep(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq; (void)opaque;
  if ((value&1) && is_output('F',2)) ++switch_model.wakes;
}

static void panel_y_step(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq; (void)opaque;
  const uint8_t level=value&1;
  const uint8_t previous=switch_model.previous_step;
  switch_model.previous_step=level;
  if (!level || previous==level || !is_output('F',6)) return;
  const int direction=gpio_level('F',7) ? 1 : -1;
  if (!switch_model.phases || switch_model.phase_direction[switch_model.phases-1]!=direction) {
    CHECK(switch_model.phases<8,"unexpected extra Y direction reversals");
    switch_model.phase_direction[switch_model.phases++]=direction;
  }
  ++switch_model.phase_steps[switch_model.phases-1];
  switch_model.position+=direction;
  if (!switch_model.enabled) return;
  const uint8_t active=switch_model.position<=0;
  if (active!=switch_model.level) {
    if (active) ++switch_model.contacts; else ++switch_model.releases;
    switch_model.level=active;
    input_pin('C',0,!active);
  }
}

static void panel_y_enable(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq;
  watched_pin_t *pin=opaque;
  if (is_output(pin->port,pin->bit)) CHECK(!(value&1),"panel woke non-Y driver P%c%u",pin->port,pin->bit);
}

static void boot_panel(const char *elf, int held)
{
  held_at_boot=held;
  boot(elf,0);
  all_asleep();
  memset(&switch_model,0,sizeof(switch_model));
  switch_model.position=150;
  avr_irq_register_notify(pin_irq('F',6),panel_y_step,NULL);
  avr_irq_register_notify(pin_irq('F',2),panel_y_sleep,NULL);
  const char ports[]={'D','K','A','C'};
  const uint8_t bits[]={7,0,2,7};
  for (unsigned i=0;i<sizeof(bits);++i)
    avr_irq_register_notify(pin_irq(ports[i],bits[i]),panel_y_enable,&sim.watch[ports[i]-'A'][bits[i]]);
  CHECK(lcd.four_bit && lcd.raw_init==3 && lcd.function_sets && lcd.display_on && lcd.entry_modes,
    "HD44780 initialization missing: init%u function%u on%u entry%u",lcd.raw_init,lcd.function_sets,lcd.display_on,lcd.entry_modes);
  CHECK(lcd.data_bytes>=80,"LCD did not write all four rows");
  CHECK(is_output('A',0) && gpio_level('A',0),"LCD backlight PA0 not HIGH output");
  for (unsigned key=0;key<KEY_COUNT;++key) {
    CHECK(!is_output(key_port[key],key_bit[key]),"key%u configured as output",key);
    CHECK(gpio_level(key_port[key],key_bit[key]),"key%u missing internal pullup",key);
  }
  CHECK(display_has("Y") && display_has("Alarm"),"startup display lacks Y/alarm state:\n%s",display_text());
}

static void press(enum panel_key key)
{
  key_level(key,0); run_us(35000);
  key_level(key,1); run_us(45000);
}

static void unlock_panel(void)
{
  press(KEY_OK);
  run_us(120000);
  CHECK(strstr(command("?",30),"<Idle"),"panel unlock did not leave Alarm:\n%s",display_text());
  CHECK(display_has("Bereit"),"unlocked display lacks BEREIT:\n%s",display_text());
  CHECK(!sim.step_count[1],"unlock emitted STEP");
}

static void select_homing(void)
{
  for (unsigned i=0;i<3;++i) press(KEY_DOWN);
  run_us(100000);
  CHECK(display_has("Y") && display_has("Refer"),"Y homing menu missing:\n%s",display_text());
}

static void confirm_homing(void)
{
  press(KEY_OK);
  CHECK(!sim.step_count[1],"first homing confirmation already moved Y");
  press(KEY_OK);
}

static void wait_steps(unsigned minimum)
{
  const uint64_t deadline=sim.avr->cycle+3*CPU_HZ;
  while (sim.step_count[1]<minimum && sim.avr->cycle<deadline) run_us(1000);
  CHECK(sim.step_count[1]>=minimum,"panel did not start Y motion:\n%s",display_text());
  no_other_steps();
}

static void test_startup_and_keys(const char *elf)
{
  boot_panel(elf,KEY_OK);
  run_us(800000);
  CHECK(strstr(command("?",30),"<Alarm") && !sim.step_count[1],"held startup OK was accepted");
  key_level(KEY_OK,1); run_us(60000);
  for (enum panel_key key=KEY_E1;key<=KEY_E4;++key) press(key);
  CHECK(!sim.step_count[1],"unused original printer key caused motion");
  char before_diagnostic[85];
  memcpy(before_diagnostic,display_text(),sizeof(before_diagnostic));
  press(KEY_RIGHT); run_us(150000);
  CHECK(strcmp(before_diagnostic,display_text()),"RIGHT did not change diagnostic display");
  CHECK(!sim.step_count[1],"diagnostic navigation caused motion");
  press(KEY_RIGHT); run_us(150000);
  for (unsigned bounce=0;bounce<6;++bounce) {
    key_level(KEY_OK,0); run_us(3000); key_level(KEY_OK,1); run_us(3000);
  }
  run_us(70000);
  CHECK(strstr(command("?",30),"<Alarm"),"bouncing OK unlocked the machine");
  unlock_panel();
  press(KEY_DOWN); press(KEY_DOWN);
  key_level(KEY_OK,0); key_level(KEY_DOWN,0); run_us(60000);
  key_level(KEY_DOWN,1); run_us(60000);
  CHECK(!sim.step_count[1],"multi-key press or partial release caused motion");
  key_level(KEY_OK,1); run_us(60000);
  all_asleep(); no_other_steps();
  printf("PASS: HD44780 init/20x4 bus/backlight/pullups; held startup OK, bounce and multiple keys cannot move/unlock\n");
  panel_terminate();
}

static void test_jogs(const char *elf)
{
  boot_panel(elf,-1); unlock_panel();
  press(KEY_DOWN);
  key_level(KEY_OK,0); run_us(1200000);
  CHECK(sim.step_count[1]==15,"held negative-jog OK emitted%u steps instead of15",sim.step_count[1]);
  CHECK(switch_model.position==135,"negative-jog direction is wrong");
  key_level(KEY_OK,1); run_us(60000);
  press(KEY_DOWN); press(KEY_OK); run_us(700000);
  CHECK(sim.step_count[1]==30 && switch_model.position==150,"positive-jog did not return15 steps");
  // The same physical key pressed during an existing jog is not a queued jog.
  // Driver wake intentionally takes25ms before the serial command returns.
  assert_ok("$J=G91G21Y0.5F20\n",50);
  press(KEY_OK); run_us(1800000);
  CHECK(sim.step_count[1]==106,"busy panel key queued a second jog, total%u",sim.step_count[1]);
  all_asleep(); no_other_steps();
  run_us(150000);
  CHECK(display_has("Bereit"),"jog did not restore BEREIT on display:\n%s",display_text());
  printf("PASS: menu -0.1/+0.1mm produces15+15 Y-only steps; held OK never repeats; busy key never queues\n");
  panel_terminate();
}

static void test_home(const char *elf)
{
  boot_panel(elf,-1);
  switch_model.enabled=1;
  select_homing();
  key_level(KEY_OK,0); run_us(800000);
  CHECK(!sim.step_count[1],"held first homing OK bypassed separate confirmation");
  key_level(KEY_OK,1); run_us(60000);
  press(KEY_OK);
  wait_steps(10);
  run_us(150000);
  CHECK(display_has("REF-SUCHE"),"moving homing state absent from LCD:\n%s",display_text());
  const uint64_t deadline=sim.avr->cycle+12*CPU_HZ;
  while ((switch_model.phases<4 || awake(1)) && sim.avr->cycle<deadline) run_us(10000);
  CHECK(sim.avr->cycle<deadline,"panel homing did not finish:\n%s",display_text());
  run_us(250000);
  CHECK(switch_model.phases==4 && switch_model.contacts==2 && switch_model.releases==2,
    "panel homing missed standard search/pull-off/locate/pull-off cycle: phases%u contacts%u releases%u",
    switch_model.phases,switch_model.contacts,switch_model.releases);
  for (unsigned p=0;p<4;++p) {
    CHECK(switch_model.phase_direction[p]==(p%2 ? 1 : -1),"panel homing phase%u direction wrong",p);
    CHECK(switch_model.phase_steps[p]>=150 && switch_model.phase_steps[p]<=155,
      "panel homing phase%u travel wrong: %u steps",p,switch_model.phase_steps[p]);
  }
  CHECK(strstr(command("?",30),"<Idle") && display_has("Bereit"),"panel homing did not return Idle/BEREIT:\n%s",display_text());
  all_asleep(); no_other_steps();
  printf("PASS: two separate OK edges run complete Y-only standard homing,2 contacts/2 releases and LCD REF-SUCHE/BEREIT updates\n");
  panel_terminate();
}

static void check_stopped(enum panel_key key)
{
  key_level(key,0);
  run_us(1200);
  all_asleep();
  const unsigned steps=sim.step_count[1];
  run_us(700000);
  CHECK(sim.step_count[1]==steps,"Y STEP continued after raw panel stop");
  CHECK(display_has("Alarm") || display_has("Abbruch"),"LCD failed to update during critical alarm wait:\n%s",display_text());
  no_other_steps();
}

static void test_stop(const char *elf, enum panel_key key, unsigned phase)
{
  boot_panel(elf,-1);
  switch_model.enabled=phase!=0;
  select_homing(); confirm_homing();
  const uint64_t deadline=sim.avr->cycle+10*CPU_HZ;
  while ((switch_model.phases<phase+1 || switch_model.phase_steps[phase]<8) && sim.avr->cycle<deadline) run_us(1000);
  CHECK(sim.avr->cycle<deadline,"homing phase%u did not start",phase);
  check_stopped(key);
  printf("PASS: raw P%c%u stops homing phase%u within one Timer2 tick; no later STEP, LCD alarm remains live\n",key_port[key],key_bit[key],phase);
  panel_terminate();
}

static void test_stop_jog(const char *elf)
{
  boot_panel(elf,-1); unlock_panel();
  press(KEY_DOWN); press(KEY_DOWN); press(KEY_OK);
  wait_steps(3);
  check_stopped(KEY_LEFT);
  CHECK(sim.step_count[1]<15,"jog stop failed to interrupt bounded movement");
  printf("PASS: LEFT stops an active panel jog without waiting for key debounce\n");
  panel_terminate();
}

static void test_fault_display(const char *elf)
{
  boot_panel(elf,-1);
  select_homing(); confirm_homing(); wait_steps(15);
  input_pin('E',6,0); run_us(1); input_pin('E',6,1);
  run_us(1800); all_asleep();
  const unsigned steps=sim.step_count[1];
  run_us(700000);
  CHECK(strstr(sim.log,"ALARM:12"),"FAULTn did not report driver fault");
  CHECK(display_has("12") && (display_has("Alarm") || display_has("Fault")),
    "driver fault code absent from LCD critical-alarm screen:\n%s",display_text());
  CHECK(sim.step_count[1]==steps,"driver fault allowed continuing Y steps");
  no_other_steps();
  printf("PASS:1us FAULTn pulse stops Y and displays retained alarm12 while serial waits\n");
  panel_terminate();
}

static void commands_blocked_by_held_stop(void)
{
  const unsigned steps=sim.step_count[1], wakes=switch_model.wakes;
  assert_error("$X\n");
  // Session configuration itself may be accepted; it must never bypass STOP.
  const char *setting=command("$22=1\n",50);
  CHECK(strstr(setting,"ok\r\n") || strstr(setting,"error:"),"session-setting command produced no response");
  assert_error("$HY\n");
  assert_error("$J=G91G21Y0.1F20\n");
  run_us(100000);
  CHECK(sim.step_count[1]==steps && switch_model.wakes==wakes,
    "held STOP allowed UART motion or motor wake: steps%u/%u wakes%u/%u",
    steps,sim.step_count[1],wakes,switch_model.wakes);
  CHECK(strstr(command("?",30),"<Alarm"),"held STOP allowed leaving startup/reset Alarm");
  all_asleep(); no_other_steps();
}

static void test_held_stop(const char *elf)
{
  const enum panel_key stops[]={KEY_LEFT,KEY_STOP1,KEY_STOP2};
  for (unsigned i=0;i<sizeof(stops)/sizeof(*stops);++i) {
    const enum panel_key key=stops[i];
    boot_panel(elf,key);
    commands_blocked_by_held_stop();
    key_level(key,1); run_us(60000);
    assert_ok("$X\n",50);
    assert_ok("$J=G91G21Y0.1F20\n",700);
    CHECK(sim.step_count[1]==15,"release of startup STOP failed to permit bounded UART jog");
    all_asleep();
    key_level(key,0); run_us(100000);
    commands_blocked_by_held_stop();
    key_level(key,1); run_us(60000);
    assert_ok("$X\n",50);
    assert_ok("$J=G91G21Y-0.1F20\n",700);
    CHECK(sim.step_count[1]==30 && switch_model.position==150,
      "release of runtime STOP failed to permit bounded reverse UART jog");
    all_asleep(); no_other_steps();
    printf("PASS: held P%c%u blocks UART unlock/home/jog at startup and after reset without motor wake; release permits bounded jog\n",
      key_port[key],key_bit[key]);
    panel_terminate();
  }
}

static void test_serial_cancels_confirmation(const char *elf)
{
  boot_panel(elf,-1);
  select_homing(); press(KEY_OK); run_us(200000);
  CHECK(display_has("Y-Referenz starten?"),"initial homing confirmation did not open:\n%s",display_text());
  CHECK(!sim.step_count[1],"opening confirmation emitted STEP");
  assert_ok("$X\n",50);
  assert_ok("$J=G91G21Y0.5F20\n",80);
  run_us(200000);
  CHECK(display_has("Y: JOG") && !display_has("starten?"),
    "serial jog retained stale homing confirmation:\n%s",display_text());
  run_us(1500000);
  CHECK(sim.step_count[1]==76 && display_has("Bereit"),
    "serial jog did not finish its bounded76 steps/BEREIT:\n%s",display_text());
  all_asleep();
  const unsigned wakes=switch_model.wakes;
  press(KEY_OK); run_us(200000);
  CHECK(display_has("Y-Referenz starten?"),"first OK after serial jog did not reopen confirmation:\n%s",display_text());
  CHECK(sim.step_count[1]==76 && switch_model.wakes==wakes,
    "serial interruption left an armed homing confirmation; new OK started motion");
  all_asleep(); no_other_steps();
  printf("PASS: UART jog cancels open homing confirmation; LCD shows JOG/BEREIT and next OK only reopens dialog\n");
  panel_terminate();
}

int main(int argc, char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) { fprintf(stderr,"usage: %s y-panel-firmware.elf [case]\n",argv[0]); return 2; }
  unsigned selected=0;
  #define RUN_CASE(name, ...) do { if (argc==2 || !strcmp(argv[2],name)) { __VA_ARGS__; ++selected; } } while (0)
  RUN_CASE("keys",test_startup_and_keys(argv[1]));
  RUN_CASE("jog",test_jogs(argv[1]));
  RUN_CASE("home",test_home(argv[1]));
  RUN_CASE("stop-search",test_stop(argv[1],KEY_LEFT,0));
  RUN_CASE("stop-pulloff",test_stop(argv[1],KEY_STOP1,1));
  RUN_CASE("stop-locate",test_stop(argv[1],KEY_STOP2,2));
  RUN_CASE("stop-final-pulloff",test_stop(argv[1],KEY_LEFT,3));
  RUN_CASE("stop-jog",test_stop_jog(argv[1]));
  RUN_CASE("fault-display",test_fault_display(argv[1]));
  RUN_CASE("held-stop",test_held_stop(argv[1]));
  RUN_CASE("serial-confirmation",test_serial_cancels_confirmation(argv[1]));
  #undef RUN_CASE
  if (!selected) { fprintf(stderr,"unknown test case: %s\n",argv[2]); return 2; }
  printf("PASS: %u local-panel real-AVR cases; retained EEPROM unchanged\n",selected);
  return 0;
}
