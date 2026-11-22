// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (src/lpc17xx/arch-eeprom.h),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// arch-eeprom.h - "EEPROM" for sd2iec on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// The Pi has no EEPROM. For now the settings (eeprom-conf.c) live in RAM:
// they last until a reboot. Later this could move to a file on the SD card.

#ifndef ARCH_EEPROM_H
#define ARCH_EEPROM_H

#include <stdint.h>

/*
 * As on lpc17xx: EEMEM variables live in the .eeprom section, which
 * linker.ld places at address 0 without contents. Their addresses are
 * therefore offsets into the "EEPROM"; the contents themselves are in
 * eeprom_ram[] (rpi/arch-eeprom.c).
 */
#define EEMEM __attribute__((section(".eeprom")))
#define EEPROM_SIZE 1024
#define eeprom_safety() do {} while (0)

uint8_t  eeprom_read_byte(void *addr);
uint16_t eeprom_read_word(void *addr);
void     eeprom_read_block(void *destptr, void *addr, unsigned int length);
void     eeprom_write_byte(void *addr, uint8_t value);
void     eeprom_write_word(void *addr, uint16_t value);
void     eeprom_write_block(void *srcptr, void *addr, unsigned int length);

#endif
