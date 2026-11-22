// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// llfl.c - low-level fast loaders for sd2iec on the Pi that are not ported
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// JiffyDOS is sd2iec's own lpc17xx/llfl-jiffydos.c on top of rpi/llfl-common.c.
// DolphinDOS needs a parallel cable, which the HAT doesn't have: iec.c calls
// dolphin_putc/dolphin_getc only when the C64 uses DolphinDOS, and then gets
// an error.

#include <stdint.h>
#include "config.h"
#include "fastloader.h"
#include "fastloader-ll.h"

int16_t dolphin_getc(void)
{
    return -1;
}

uint8_t dolphin_putc(uint8_t data, uint8_t with_eoi)
{
    (void)data; (void)with_eoi;
    return 1;
}
