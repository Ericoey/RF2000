# RF2000v2 / I851 CTC V3 development build. No automatic flash or fuse changes.
TOOLCHAIN_ROOT ?= /workspace/.tools/avr
AVR_PREFIX ?= $(TOOLCHAIN_ROOT)/usr/bin/avr-
CC := $(AVR_PREFIX)gcc
OBJCOPY := $(AVR_PREFIX)objcopy
OBJDUMP := $(AVR_PREFIX)objdump
SIZE := $(AVR_PREFIX)size
MCU := atmega2560
BUILD := build
ARTIFACTS := artifacts/grbl
SOURCES := $(wildcard grbl/*.c)
OBJECTS := $(patsubst grbl/%.c,$(BUILD)/%.o,$(SOURCES))
CPPFLAGS += -DF_CPU=16000000UL -Igrbl
CFLAGS += -std=gnu99 -Os -g -Wall -Wextra -mmcu=$(MCU) -ffunction-sections -fdata-sections -flto
LDFLAGS += -mmcu=$(MCU) -flto -Wl,--gc-sections,-Map,$(BUILD)/main.map

.PHONY: all clean check check-y-homing check-panel y-homing-test disasm repetier flash fuse install load
all: $(ARTIFACTS)/rf2000-grbl.hex $(ARTIFACTS)/rf2000-grbl.elf $(ARTIFACTS)/rf2000-grbl.map

$(BUILD) $(ARTIFACTS):
	mkdir -p $@

$(BUILD)/%.o: grbl/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/main.elf: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(SIZE) $@
	python3 tools/check-avr-size.py $@ $(SIZE)

$(ARTIFACTS)/rf2000-grbl.hex: $(BUILD)/main.elf | $(ARTIFACTS)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

$(ARTIFACTS)/rf2000-grbl.elf: $(BUILD)/main.elf | $(ARTIFACTS)
	cp $< $@

$(ARTIFACTS)/rf2000-grbl.map: $(BUILD)/main.elf | $(ARTIFACTS)
	cp $(BUILD)/main.map $@

disasm: $(BUILD)/main.elf
	$(OBJDUMP) -d -S $< > $(BUILD)/rf2000-grbl.lst

check: all
	python3 tools/test_check_avr_size.py
	python3 tools/test_rf2000_limits.py
	python3 tools/test_rf2000_drv8711.py
	python3 tools/test_eeprom_records.py
	python3 tools/test_serial_ring.py
	tests/run-sim.sh

repetier:
	tools/build-repetier.sh

y-homing-test:
	$(MAKE) BUILD=build/y-homing ARTIFACTS=artifacts/y-homing CPPFLAGS='$(CPPFLAGS) -DRF2000_Y_HOMING_TEST=1' all

check-y-homing: y-homing-test
	tests/run-y-homing-sim.sh build/y-homing/main.elf

check-panel: y-homing-test
	tests/run-panel-sim.sh build/y-homing/main.elf

flash fuse install load:
	@echo 'No automatic flashing or fuse writing. Read docs/hardware-test.md first.' >&2
	@exit 1

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.d $(BUILD)/*.elf $(BUILD)/*.map $(BUILD)/*.lst
	rm -f $(ARTIFACTS)/rf2000-grbl.hex $(ARTIFACTS)/rf2000-grbl.elf $(ARTIFACTS)/rf2000-grbl.map

-include $(OBJECTS:.o=.d)
