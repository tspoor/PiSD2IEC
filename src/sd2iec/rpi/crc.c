// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// crc.c - CRC routines for sd2iec on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Same results as avr-libc (util/crc16.h) and sd2iec's lpc17xx/crc.S:
//  crc16_update:      CRC-16 (poly 0xA001, reflected)
//  crc_xmodem_update: CRC-CCITT/XMODEM (poly 0x1021)
//  crc7update:        CRC7 of SD commands (poly 0x09)

#include "crc.h"

uint16_t crc16_update(uint16_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++)
        crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    return crc;
}

uint16_t crc_xmodem_update(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t crc_xmodem_block(uint16_t crc, const uint8_t *data, uint32_t length)
{
    while (length--)
        crc = crc_xmodem_update(crc, *data++);
    return crc;
}

uint8_t crc7update(uint8_t crc, uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        crc <<= 1;
        if ((data ^ crc) & 0x80)
            crc ^= 0x09;
        data <<= 1;
    }
    return crc & 0x7F;
}
