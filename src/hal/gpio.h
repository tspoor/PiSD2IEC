// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// gpio.h - GPIO of the BCM2710
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include "rpi.h"

enum {
    GPIO_IN   = 0, GPIO_OUT  = 1,
    GPIO_ALT0 = 4, GPIO_ALT1 = 5, GPIO_ALT2 = 6,
    GPIO_ALT3 = 7, GPIO_ALT4 = 3, GPIO_ALT5 = 2,
};

enum { GPIO_PULL_NONE = 0, GPIO_PULL_DOWN = 1, GPIO_PULL_UP = 2 };

void gpio_fsel(unsigned pin, unsigned func);
void gpio_pull(unsigned pin, unsigned mode);

static inline void gpio_set(unsigned pin)
{
    if (pin < 32) GPSET0 = 1u << pin; else GPSET1 = 1u << (pin - 32);
}

static inline void gpio_clr(unsigned pin)
{
    if (pin < 32) GPCLR0 = 1u << pin; else GPCLR1 = 1u << (pin - 32);
}

static inline unsigned gpio_read(unsigned pin)
{
    return pin < 32 ? (GPLEV0 >> pin) & 1 : (GPLEV1 >> (pin - 32)) & 1;
}

#endif
