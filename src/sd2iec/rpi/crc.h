// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// crc.h - CRC routines for sd2iec on the Pi (in C, see rpi/crc.c)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef CRC_H
#define CRC_H

#include <stdint.h>

uint8_t  crc7update(uint8_t crc, uint8_t data);
uint16_t crc_xmodem_update(uint16_t crc, uint8_t data);
uint16_t crc_xmodem_block(uint16_t crc, const uint8_t *data, uint32_t length);
uint16_t crc16_update(uint16_t crc, uint8_t data);

#endif
