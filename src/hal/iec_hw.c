// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// iec_hw.c - initialisation of the IEC pins
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "gpio.h"
#include "iec_hw.h"

void iec_hw_init(void)
{
    static const unsigned char in_pins[] = {
        HAT_IN_ATN, HAT_IN_CLOCK, HAT_IN_DATA, HAT_IN_SRQ, HAT_IN_RESET
    };
    static const unsigned char out_pins[] = {
        HAT_OUT_ATN, HAT_OUT_CLOCK, HAT_OUT_DATA, HAT_OUT_SRQ
    };

    /*
     * Inputs with pull-up. The inputs go through the BSS138 level shifter
     * on the HAT; a pull-down (the BCM default, as Pi1541 uses) also pulls
     * the bus line down through that shifter. On a MiSTer with a SNAC
     * without level shifter that put the level near the FPGA threshold.
     * With pull-up the Pi side stays firmly high and the shifter stays off.
     */
    for (unsigned i = 0; i < sizeof(in_pins); i++) {
        gpio_fsel(in_pins[i], GPIO_IN);
        gpio_pull(in_pins[i], GPIO_PULL_UP);
    }

    /* Set the output level to "released" first, then switch to output,
       so the bus is never pulled low for a moment at startup. */
    iec_out(IEC_OMASK_ALL, 1);
    for (unsigned i = 0; i < sizeof(out_pins); i++) {
        gpio_pull(out_pins[i], GPIO_PULL_DOWN);
        gpio_fsel(out_pins[i], GPIO_OUT);
    }

    /* Pi1541 sets GPIO 9 (PIGPIO_OUT_SPI0_RS) as an output, low */
    gpio_clr(9);
    gpio_fsel(9, GPIO_OUT);
}
