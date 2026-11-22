// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// petscii.h - PETSCII to ASCII for the OLED (rpi/display.c)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// names = 1: file and directory names, as read in lowercase/uppercase mode
// ("Games"); names = 0: messages and commands, in capitals ("00, OK").
// Stops at 0 or CR, skips 0xA0 padding, removes trailing spaces.

#ifndef PETSCII_H
#define PETSCII_H

#include <stdint.h>

void rpi_copy_pet(char *dst, unsigned max, const uint8_t *src, unsigned len, int names);

#endif
