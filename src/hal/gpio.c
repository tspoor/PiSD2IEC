// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// gpio.c - pin function and pull-up/down (BCM2837 method)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "gpio.h"
#include "timer.h"

void gpio_fsel(unsigned pin, unsigned func)
{
    unsigned reg   = pin / 10;
    unsigned shift = (pin % 10) * 3;
    uint32_t v = GPFSEL(reg);
    v &= ~(7u << shift);
    v |= (func & 7u) << shift;
    GPFSEL(reg) = v;
}

void gpio_pull(unsigned pin, unsigned mode)
{
    GPPUD = mode;
    delay_us(2);                         /* >= 150 clock cycles */
    if (pin < 32) GPPUDCLK0 = 1u << pin; else GPPUDCLK1 = 1u << (pin - 32);
    delay_us(2);
    GPPUD = 0;
    GPPUDCLK0 = 0;
    GPPUDCLK1 = 0;
}
