// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// arch-eeprom.c - "EEPROM" in RAM (see arch-eeprom.h)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Addresses are offsets (EEMEM section at address 0); beyond EEPROM_SIZE
// this reads 0xFF and ignores writes, like an empty EEPROM.

#include <stdint.h>
#include "arch-eeprom.h"

/* Empty, like an erased EEPROM (0xFF): sd2iec then uses its defaults */
static uint8_t eeprom_ram[EEPROM_SIZE] = { [0 ... EEPROM_SIZE - 1] = 0xFF };

static inline int valid(uintptr_t a)
{
    return a < EEPROM_SIZE;
}

uint8_t eeprom_read_byte(void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    return valid(a) ? eeprom_ram[a] : 0xFF;
}

uint16_t eeprom_read_word(void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    return (uint16_t)(eeprom_read_byte((void *)a) |
                      (eeprom_read_byte((void *)(a + 1)) << 8));
}

void eeprom_read_block(void *destptr, void *addr, unsigned int length)
{
    uint8_t *d = destptr;
    uintptr_t a = (uintptr_t)addr;
    while (length--)
        *d++ = eeprom_read_byte((void *)a++);
}

void eeprom_write_byte(void *addr, uint8_t value)
{
    uintptr_t a = (uintptr_t)addr;
    if (valid(a))
        eeprom_ram[a] = value;
}

void eeprom_write_word(void *addr, uint16_t value)
{
    uintptr_t a = (uintptr_t)addr;
    eeprom_write_byte((void *)a, (uint8_t)value);
    eeprom_write_byte((void *)(a + 1), (uint8_t)(value >> 8));
}

void eeprom_write_block(void *srcptr, void *addr, unsigned int length)
{
    const uint8_t *s = srcptr;
    uintptr_t a = (uintptr_t)addr;
    while (length--)
        eeprom_write_byte((void *)a++, *s++);
}
