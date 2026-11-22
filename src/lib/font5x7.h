// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// font5x7.h - 5x7 font for ASCII 0x20..0x7E (5 columns per character, bit 0 = top)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

#define FONT5X7_FIRST 0x20
#define FONT5X7_LAST  0x7E

extern const uint8_t font5x7[FONT5X7_LAST - FONT5X7_FIRST + 1][5];

#endif
