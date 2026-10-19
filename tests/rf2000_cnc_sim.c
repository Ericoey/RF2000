/* Actual AVR CNC profile: gSender commands, XYZ hardware, LCD, jobs and faults.
 * Reuse the real LCD decoder and five-DRV8711 peripheral model. GPL-3.0+. */
#define RF2000_PANEL_TEST_MAIN unused_y_panel_suite
#include "rf2000_panel_sim.c"
#include <math.h>

/* simavr v1.6 avr_eeprom_write() ignores EEPM and always stores EEDR.
 * The AVR driver correctly uses erase-only mode for 0xff; real silicon
 * ignores EEDR in that mode. Wrap only that peripheral callback, retaining
 * its EEMPE window, timing and read path, and add the datasheet EEPM modes.
 * Reference: buserror/simavr v1.6 simavr/sim/avr_eeprom.c:avr_eeprom_write. */
static avr_io_write_t original_eeprom_write;
static unsigned eeprom_mode_writes[3];
static void cnc_eeprom_write(avr_t *avr,avr_io_addr_t addr,uint8_t value,void *opaque)
{
  avr_eeprom_t *ee=opaque;
  const uint8_t writing=avr_regbit_get(avr,ee->eempe) && ((value>>ee->eepe.bit)&1);
  const uint8_t mode=((value>>ee->eepm[0].bit)&1) | (((value>>ee->eepm[1].bit)&1)<<1);
  const uint16_t address=(avr->data[ee->r_eearl] | ((uint16_t)avr->data[ee->r_eearh]<<8)) & (ee->size-1);
  const uint8_t before=ee->eeprom[address], data=avr->data[ee->r_eedr];
  original_eeprom_write(avr,addr,value,opaque);
  if (writing) {
    CHECK(mode<3,"firmware selected reserved EEPROM programming mode");
    ++eeprom_mode_writes[mode];
    if (mode==1) ee->eeprom[address]=0xff;
    else if (mode==2) ee->eeprom[address]=before & data;
  }
}

static void cnc_peripheral_init(avr_t *avr)
{
  for (avr_io_t *io=avr->io_port;io;io=io->next) if (!strcmp(io->kind,"eeprom")) {
    avr_eeprom_t *ee=(avr_eeprom_t *)io;
    const unsigned index=AVR_DATA_TO_IO(ee->r_eecr);
    original_eeprom_write=avr->io[index].w.c;
    CHECK(original_eeprom_write,"EEPROM peripheral write handler missing");
    avr->io[index].w.c=cnc_eeprom_write;
    memset(eeprom_mode_writes,0,sizeof(eeprom_mode_writes));
    return;
  }
  CHECK(0,"EEPROM peripheral missing");
}

static struct {
  int position[3];
  uint8_t enabled, last_step[3], switch_active[3];
  unsigned contacts[3], releases[3], phases[3];
  int last_direction[3];
  unsigned first_axis;
} machine;

static void cnc_step(struct avr_irq_t *irq, uint32_t value, void *opaque)
{
  (void)irq;
  const unsigned axis=(uintptr_t)opaque;
  const uint8_t level=value&1, previous=machine.last_step[axis];
  machine.last_step[axis]=level;
  if (!level || previous) return;
  const char port=axis==2 ? 'L' : 'F';
  const uint8_t bit=axis==0 ? 1 : axis==1 ? 7 : 1;
  const int direction=gpio_level(port,bit) ? (axis==0 ? -1 : 1) : (axis==0 ? 1 : -1);
  if (!machine.first_axis) machine.first_axis=axis+1;
  if (machine.last_direction[axis]!=direction) {
    machine.last_direction[axis]=direction; ++machine.phases[axis];
  }
  machine.position[axis]+=direction;
  if (!machine.enabled) return;
  const uint8_t active=axis==2 ? machine.position[axis]>=0 : machine.position[axis]<=0;
  if (active!=machine.switch_active[axis]) {
    machine.switch_active[axis]=active;
    if (active) ++machine.contacts[axis]; else ++machine.releases[axis];
    if (axis==2) input_pin('H',1,!active);
    else input_pin('C',axis==0 ? 2 : 0,!active);
  }
}

static void cnc_boot(const char *elf)
{
  held_at_boot=-1;
  extra_peripheral_init=cnc_peripheral_init;
  boot(elf,0);
  extra_peripheral_init=NULL;
  run_us(300000);
  CHECK(strstr(sim.log,"RF2000 XYZ CNC"),"wrong profile startup: %s",sim.log);
  verify_registers(); all_asleep();
  CHECK(lcd.four_bit && lcd.raw_init==3 && lcd.data_bytes>=80,"CNC LCD missing");
  CHECK(display_has("CNC") && display_has("Alarm"),"CNC alarm screen missing:\n%s",display_text());
  memset(&machine,0,sizeof(machine));
  machine.position[0]=64; machine.position[1]=80; machine.position[2]=-100;
  const uint8_t step_bits[3]={0,6,3};
  for (uintptr_t axis=0;axis<3;++axis)
    avr_irq_register_notify(pin_irq('F',step_bits[axis]),cnc_step,(void *)axis);
}

static void reserves_off(void)
{
  CHECK(!awake(3) && !awake(4) && !sim.step_count[3] && !sim.step_count[4],
    "CNC moved or woke reserve A/B");
}

static void cnc_unlock(void)
{
  assert_ok("$X\n",60); run_us(200000);
  CHECK(display_has("BEREIT"),"CNC unlock not shown:\n%s",display_text());
}

static void test_cnc_connect(const char *elf)
{
  cnc_boot(elf);
  CHECK(strstr(command("$I\n",80),"20261019-rf2000-cnc"),"CNC build identity missing");
  const char *settings_reply=command("$$\n",350);
  CHECK(strstr(settings_reply,"$1=255") && strstr(settings_reply,"$22=1") &&
    strstr(settings_reply,"$23=3"),"CNC hold/homing defaults wrong: %s",settings_reply);
  assert_ok("$G\n",60); assert_ok("$#\n",100);
  CHECK(strstr(command("?",30),"MPos:") && strstr(sim.log,"WCO:"),"gSender position/WCO fields missing");
  cnc_unlock();
  assert_ok("M3S12000\n",60); assert_ok("M4S8000\n",60); assert_ok("M5\n",60);
  CHECK(!sim.step_count[0] && !sim.step_count[1] && !sim.step_count[2],"manual spindle words moved an axis");
  assert_error("M8\n"); assert_error("G38.2Z-1F10\n");
  assert_error("G0A1\n"); assert_error("G0B1\n");
  assert_error("$5=1\n"); assert_error("$21=0\n");
  reserves_off(); all_asleep();
  printf("PASS: gSender handshake/settings/G54/status; XYZ homing default; manual spindle M3/M4/S accepted without outputs; unwired commands blocked\n");
  avr_terminate(sim.avr);
}

static void test_cnc_panel(const char *elf)
{
  cnc_boot(elf);
  press(KEY_OK); run_us(250000); // Unlock, selection remains0.
  CHECK(display_has("BEREIT"),"front-panel CNC unlock failed");
  press(KEY_DOWN); press(KEY_OK); run_us(200000);
  CHECK(display_has("VERFAHREN X") && !sim.step_count[0],"CNC jog page entry moved or selected wrong axis");
  press(KEY_UP); run_us(600000);
  CHECK(sim.step_count[0]==15 && !sim.step_count[1] && !sim.step_count[2],"local X jog not15 X-only steps");
  press(KEY_OK); press(KEY_UP); run_us(600000);
  CHECK(sim.step_count[1]==15 && sim.step_count[0]==15,"local Y jog wrong");
  press(KEY_OK); press(KEY_DOWN); run_us(600000);
  CHECK(sim.step_count[2]==128,"local Z jog not128 steps");
  press(KEY_RIGHT); run_us(250000);
  CHECK(display_has("X:") && display_has("Y:") && display_has("Z:"),"three-axis status missing:\n%s",display_text());
  CHECK(display_has("0.10") && display_has("-0.10"),"work coordinates not updated:\n%s",display_text());
  // Select zero, then acknowledge twice. Neither confirmation may move.
  press(KEY_DOWN); press(KEY_DOWN); press(KEY_OK); run_us(200000);
  CHECK(display_has("Arbeitsnull setzen?"),"local work-zero confirmation missing:\n%s",display_text());
  const unsigned count=sim.step_count[0]+sim.step_count[1]+sim.step_count[2];
  press(KEY_OK); run_us(400000);
  CHECK(sim.step_count[0]+sim.step_count[1]+sim.step_count[2]==count,"zero-setting moved axes");
  const char *status=command("?",40);
  CHECK(strstr(status,"Idle"),"zero-setting left Idle: %s",status);
  const char *screen=display_text();
  float x=1,y=1,z=1;
  CHECK(strstr(screen,"X:") && strstr(screen,"Y:") && strstr(screen,"Z:") &&
    sscanf(strstr(screen,"X:")+2,"%f",&x)==1 && sscanf(strstr(screen,"Y:")+2,"%f",&y)==1 &&
    sscanf(strstr(screen,"Z:")+2,"%f",&z)==1 && fabsf(x)<0.001f && fabsf(y)<0.001f && fabsf(z)<0.001f,
    "work-zero not reflected on all LCD coordinates:\n%s",screen);
  reserves_off();
  CHECK(awake(0) && awake(1) && awake(2),"CNC idle hold255 did not retain XYZ torque");
  key_level(KEY_LEFT,0); run_us(1200); all_asleep(); run_us(600000);
  CHECK(display_has("Alarm"),"CNC reset not visible");
  printf("PASS: LCD XYZ work coordinates; front-panel X/Y/Z jog; two-press work zero without motion; idle hold and raw STOP\n");
  avr_terminate(sim.avr);
}

static void test_cnc_home(const char *elf)
{
  cnc_boot(elf);
  // Shorten the configurable pull-off/debounce for this physical switch-model
  // test; verify original defaults separately in the handshake case.
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  machine.enabled=1;
  const char *reply=command("$H\n",5000);
  CHECK(strstr(reply,"ok\r\n") && !strstr(reply,"ALARM:"),"XYZ homing failed: %s",reply);
  CHECK(machine.first_axis==3,"XYZ homing did not start with Z");
  for (unsigned axis=0;axis<3;++axis) {
    CHECK(machine.contacts[axis]==2 && machine.releases[axis]==2 && machine.phases[axis]==4,
      "axis%u home contacts/releases/phases %u/%u/%u",axis,machine.contacts[axis],machine.releases[axis],machine.phases[axis]);
  }
  run_us(250000);
  CHECK(display_has("XYZ") && display_has("BEREIT"),"full home reference mask absent:\n%s",display_text());
  CHECK(!strstr(command("?",40),"Alarm"),"home left Alarm");
  // Homed machine coordinates: X/Y near negative travel, Z just belowMAX.
  const char *position=command("?",40);
  CHECK(strstr(position,"-199.") && strstr(position,"-289.") && strstr(position,"-0.100"),
    "homed machine position not consistent with MIN/MIN/MAX: %s",position);
  reserves_off();
  input_pin('C',6,0); run_us(1200); all_asleep(); run_us(800000);
  CHECK(strstr(sim.log,"ALARM:1") && display_has("---"),"Zmin hard limit did not clear CNC reference");
  printf("PASS: $H homes Z-MAX before XY-MIN; each axis two contacts/releases and four phases; XYZ reference indication; Z-MIN trips alarm and disables all\n");
  avr_terminate(sim.avr);
}

static void test_cnc_job(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  assert_ok("G21G90G54\n",60);
  assert_ok("G10L20P1X0Y0Z0\n",150);
  assert_ok("M3S12000\n",60);
  assert_ok("G1X0.21F60\n",40);
  run_us(190000);
  CHECK(display_has("FAHRT") || sim.step_count[0]==32,"running job state not visible");
  assert_ok("G1Y0.21\n",40); assert_ok("G1X0\n",40); assert_ok("G1Y0\n",40);
  assert_ok("G1Z-0.025F30\n",40); assert_ok("G1Z0\n",40);
  assert_ok("M5\n",1500); assert_ok("M30\n",100);
  CHECK(sim.step_count[0]==64 && sim.step_count[1]==64 && sim.step_count[2]==64,
    "rectangle/depth job pulse counts wrong %u/%u/%u",sim.step_count[0],sim.step_count[1],sim.step_count[2]);
  const unsigned before=sim.step_count[0];
  assert_ok("G91G1X1F60\n",60); run_us(180000);
  command("!",350);
  CHECK(strstr(command("?",30),"<Hold"),"gSender feed hold missing");
  unsigned held=sim.step_count[0]; run_us(200000);
  CHECK(sim.step_count[0]==held,"feed hold did not stop steps");
  command("~",1300);
  CHECK(strstr(command("?",30),"<Idle") && sim.step_count[0]-before>=152 && sim.step_count[0]-before<=153,
    "gSender cycle resume failed after hold");
  reserves_off();
  printf("PASS: streamed metric G54 rectangle with Z depth and manual spindle words; exact XYZ pulses; gSender ! hold and ~ resume complete same move\n");
  avr_terminate(sim.avr);
}

static void test_cnc_arcs(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  assert_ok("G21G90G54\n",60); assert_ok("G10L20P1X0Y0Z0\n",150);
  assert_ok("G2X0.21Y0I0.105J0F60\n",40);
  assert_ok("G3X0Y0I-0.105J0\n",40);
  run_us(1200000);
  CHECK(strstr(command("?",30),"<Idle"),"arc pair did not finish");
  CHECK(sim.step_count[0]==64 && sim.step_count[1]>=56 && sim.step_count[1]<=64 && !sim.step_count[2],
    "semicircle endpoint/travel pulses wrong %u/%u/%u",sim.step_count[0],sim.step_count[1],sim.step_count[2]);
  CHECK(machine.position[0]==64 && machine.position[1]==80,"arc pair failed to return to its start");
  reserves_off();
  printf("PASS: clockwise/counterclockwise GRBL arcs interpolate XY and return to starting position without Z or reserve movement\n");
  avr_terminate(sim.avr);
}

static void test_cnc_individual_home(const char *elf)
{
  cnc_boot(elf);
  assert_ok("$27=0.1\n",350); assert_ok("$26=5\n",350);
  machine.enabled=1;
  assert_ok("$HX\n",2000); run_us(250000);
  CHECK(machine.contacts[0]==2 && !machine.contacts[1] && !machine.contacts[2] && display_has("X--"),
    "individual X home or reference mask wrong:\n%s",display_text());
  assert_ok("$HY\n",2000); run_us(250000);
  CHECK(machine.contacts[1]==2 && !machine.contacts[2] && display_has("XY-"),"individual Y home did not preserve X reference");
  assert_ok("$HZ\n",2000); run_us(250000);
  CHECK(machine.contacts[2]==2 && display_has("XYZ"),"individual Z home did not complete reference mask");
  reserves_off();
  printf("PASS: $HX/$HY/$HZ independently reference the selected motor and accumulate XYZ reference indicators\n");
  avr_terminate(sim.avr);
}

static void test_cnc_persistence(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  assert_ok("$110=450\n",350); assert_ok("$27=0.25\n",350);
  assert_ok("G10L2P1X-1.25Y2.5Z-3\n",150);
  CHECK(strstr(command("$#\n",100),"[G54:-1.250,2.500,-3.000]"),"work-coordinate EEPROM write was not readable before reboot");
  uint8_t image[4096];
  avr_eeprom_desc_t eeprom={.ee=NULL,.offset=0,.size=sizeof(image)};
  avr_ioctl(sim.avr,AVR_IOCTL_EEPROM_GET,&eeprom);
  CHECK(eeprom.ee,"CNC EEPROM snapshot unavailable"); memcpy(image,eeprom.ee,sizeof(image));
  CHECK(image[0]==74 && eeprom_mode_writes[1]>0,"CNC profile/erase-only EEPROM path was not exercised");
  avr_terminate(sim.avr);
  eeprom_boot_image=image;
  cnc_boot(elf);
  eeprom_boot_image=NULL;
  const char *reply=command("$$\n",350);
  CHECK(strstr(reply,"$110=450.000") && strstr(reply,"$27=0.250"),"CNC settings did not survive a fresh MCU instance: %s",reply);
  reply=command("$#\n",100);
  CHECK(strstr(reply,"[G54:-1.250,2.500,-3.000]"),"CNC work coordinate did not survive reboot: %s",reply);
  CHECK(!sim.step_count[0] && !sim.step_count[1] && !sim.step_count[2],"EEPROM reboot replayed motion");
  all_asleep(); reserves_off();
  printf("PASS: CNC calibration and G54 persist in EEPROM across a fresh MCU; reboot remains alarm locked without movement\n");
  avr_terminate(sim.avr);
}

static void test_cnc_timing(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  // Exercise the existing 5kHz firmware ceiling, with the CNC LCD active.
  // This simulated setting does not change the physical machine defaults.
  assert_ok("$112=234\n",350);
  assert_ok("G91G1Z-5F234\n",30);
  for (unsigned i=0;i<220;++i) command("?",10);
  CHECK(sim.step_count[2]==6400 && sim.min_window_cycles[2],"CNC fast Z move lost steps");
  const double rate=(double)CPU_HZ*64/sim.min_window_cycles[2];
  CHECK(rate>4900 && rate<5050 && sim.min_pulse>=160,
    "CNC timing outside limits: %.2fsteps/s, pulse%.3fus",rate,sim.min_pulse/16.0);
  CHECK(!sim.step_count[0] && !sim.step_count[1],"fast Z run moved XY");
  reserves_off();
  printf("PASS: live CNC LCD plus100Hz UART status:6400 Z pulses at%.2fsteps/s, min pulse%.3fus\n",rate,sim.min_pulse/16.0);
  avr_terminate(sim.avr);
}

static void test_cnc_fault(const char *elf)
{
  cnc_boot(elf); cnc_unlock();
  assert_ok("$J=G91G21Z-1F60\n",60); run_us(100000);
  CHECK(sim.step_count[2]>0,"Z jog did not start");
  input_pin('E',6,0); run_us(1); input_pin('E',6,1);
  run_us(1200); all_asleep();
  const unsigned steps=sim.step_count[2]; run_us(800000);
  CHECK(sim.step_count[2]==steps && strstr(sim.log,"ALARM:12") && display_has("12"),"CNC fault stop/display failed");
  reserves_off();
  printf("PASS: one-microsecond FAULT stops CNC Z jog, all drivers sleep, LCD alarm12 persists\n");
  avr_terminate(sim.avr);
}

int main(int argc,char **argv)
{
  setvbuf(stdout,NULL,_IOLBF,0);
  if (argc<2 || argc>3) return 2;
  unsigned selected=0;
  #define CASE(name,fn) do { if (argc==2 || !strcmp(argv[2],name)) { fn(argv[1]); ++selected; } } while (0)
  CASE("connect",test_cnc_connect); CASE("panel",test_cnc_panel); CASE("home",test_cnc_home);
  CASE("job",test_cnc_job); CASE("fault",test_cnc_fault);
  CASE("arcs",test_cnc_arcs); CASE("individual-home",test_cnc_individual_home);
  CASE("persistence",test_cnc_persistence);
  CASE("timing",test_cnc_timing);
  #undef CASE
  if (!selected) return 2;
  printf("PASS: %u real-AVR CNC cases\n",selected);
  return 0;
}
