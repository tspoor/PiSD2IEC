# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 T. Spoor
#
# Makefile - builds PiSD2IEC for a bare-metal Raspberry Pi Zero 2 W (AArch64)
# Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
#
# Build:            make
# SD card files:    make sdcard   -> build/sdcard/
# Serial upload:    make upload   (raspbootcom to imgrecv on the Pi)

CROSS   ?= aarch64-none-elf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size

BUILD   := build
TARGET  := kernel8

SRCS_C  := $(wildcard src/*.c src/hal/*.c src/lib/*.c) \
           $(filter-out src/emu/drive1541.c,$(wildcard src/emu/*.c))
SRCS_S  := $(wildcard src/hal/*.S)

# sd2iec: unchanged files from sd2iec/src plus the Pi layer (rpi/)
SD2IEC_SRC := ff.c diskio.c \
              buffers.c fatops.c fileops.c errormsg.c doscmd.c d64ops.c \
              diskchange.c eeprom-conf.c parser.c utils.c led.c timer.c m2iops.c \
              iec.c fastloader.c \
              lpc17xx/llfl-jiffydos.c
SD2IEC_C := $(addprefix src/sd2iec/,$(SD2IEC_SRC)) $(wildcard src/sd2iec/rpi/*.c)

OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C) $(SD2IEC_C)) \
           $(patsubst src/%.S,$(BUILD)/%.o,$(SRCS_S))
DEPS    := $(OBJS:.o=.d)

ARCHFLAGS := -mcpu=cortex-a53 -mstrict-align -mno-outline-atomics
INCLUDES  := -Isrc -Isrc/hal -Isrc/lib

CFLAGS  := $(ARCHFLAGS) $(INCLUDES) -std=gnu11 -O2 -g \
           -Wall -Wextra -ffreestanding -fno-stack-protector \
           -fno-tree-loop-distribute-patterns \
           -ffunction-sections -fdata-sections -MMD -MP
# sd2iec has its own headers with the same names as PiSD2IEC's (timer.h,
# uart.h, i2c.h), so it gets its own include path. PiSD2IEC headers are
# reached through "hal/..." and "lib/...". lpc17xx/ holds sd2iec's fast
# loader files that the Pi reuses unchanged (llfl-common.h and friends).
SD2IEC_INCLUDES := -Isrc/sd2iec/rpi -Isrc/sd2iec -Isrc/sd2iec/lpc17xx -Isrc
SD2IEC_CFLAGS := $(ARCHFLAGS) $(SD2IEC_INCLUDES) -std=gnu11 -O2 -g \
           -Wall -Wno-int-to-pointer-cast -ffreestanding -fno-stack-protector \
           -fno-tree-loop-distribute-patterns \
           -ffunction-sections -fdata-sections -MMD -MP
ASFLAGS := $(ARCHFLAGS) $(INCLUDES) -g -MMD -MP
LDFLAGS := -nostdlib -nostartfiles -T linker.ld -Wl,--gc-sections \
           -Wl,--no-warn-rwx-segments \
           -Wl,-Map=$(BUILD)/$(TARGET).map
# libc (newlib) for sd2iec: strchr, strcpy, ctype etc. Linked after our own
# objects, so PiSD2IEC's memcpy/memset from lib/string.c take precedence.
LIBS    := -lc -lgcc

all: $(BUILD)/$(TARGET).img

# The 1541 emulation runs once per microsecond: optimise it harder. It
# includes src/emu/drive1541.c (see there).
$(BUILD)/sd2iec/rpi/emu1541.o: SD2IEC_CFLAGS += -O3
$(BUILD)/sd2iec/rpi/emu1541.o: src/emu/drive1541.c src/emu/cpu6502_impl.h src/emu/via6522.h

$(BUILD)/$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(SIZE) $@

$(BUILD)/$(TARGET).img: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	$(OBJDUMP) -d $< > $(BUILD)/$(TARGET).lst

$(BUILD)/sd2iec/%.o: src/sd2iec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SD2IEC_CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

sdcard: all
	@mkdir -p $(BUILD)/sdcard
	cp boot/config.txt $(BUILD)/$(TARGET).img $(BUILD)/sdcard/
	@echo "Also copy bootcode.bin, start.elf and fixup.dat (see README.md)."

# Send the image with raspbootcom to the serial bootloader imgrecv on the Pi;
# raspbootcom then stays open as the console (quit with Ctrl-C)
PORT        ?= /dev/ttyUSB0
RASPBOOTCOM ?= raspbootcom
upload: all
	$(RASPBOOTCOM) $(PORT) $(BUILD)/$(TARGET).img

# ----- tests on the PC (not part of the firmware) -----
# The test data is not part of PiSD2IEC:
#   DORMANN = Klaus Dormann's 6502_functional_test.bin
#   HARTE   = folder with Tom Harte's SingleStepTests 65x02/6502/v1/*.json
#   ROM     = a 1541 DOS ROM (16 KB), e.g. VICE's dos1541
#   D64     = any D64: LOAD"$" and the first file are read through the drive
HOSTCC  ?= gcc
TESTLIB := $(BUILD)/host/libcpu6502test.so

$(TESTLIB): tests/emu/cpu_testlib.c src/emu/cpu6502_impl.h src/emu/cpu6502.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -shared -fPIC -o $@ $<

$(BUILD)/host/via_test: tests/emu/via_test.c src/emu/via6522.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -o $@ $<

$(BUILD)/host/drive_test: tests/emu/drive_test.c src/emu/*.c src/emu/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -o $@ tests/emu/drive_test.c src/emu/drive1541.c src/emu/disk1541.c

# make test-emu [DORMANN=...] [HARTE=...] [ROM=1541 DOS ROM, 16 KB] [D64=disk image]
test-emu: test-cpu $(BUILD)/host/via_test $(BUILD)/host/drive_test
	$(BUILD)/host/via_test
ifdef ROM
	$(BUILD)/host/drive_test $(ROM) $(D64)
endif

test-cpu: $(TESTLIB)
ifdef DORMANN
	python3 tests/emu/cpu_test.py $(TESTLIB) dormann $(DORMANN)
endif
ifdef HARTE
	python3 tests/emu/cpu_test.py $(TESTLIB) harte $(HARTE)
endif

clean:
	rm -rf $(BUILD)

-include $(DEPS)

.PHONY: all clean sdcard upload test-cpu test-emu
