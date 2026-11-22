// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// timer.c - time functions based on CNTPCT_EL0
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "timer.h"

static uint64_t freq = 19200000;     /* default for Pi 3/Zero 2; see timer_init() */

void timer_init(void)
{
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    if (f)
        freq = f;
}

uint64_t timer_freq(void)
{
    return freq;
}

uint64_t timer_us(void)
{
    uint64_t t = timer_ticks();
    return (t / freq) * 1000000u + ((t % freq) * 1000000u) / freq;
}

uint64_t timer_us_to_ticks(uint32_t us)
{
    return ((uint64_t)us * freq) / 1000000u;
}

void delay_us(uint32_t us)
{
    uint64_t end = timer_ticks() + timer_us_to_ticks(us);
    while (timer_ticks() < end)
        ;
}

void delay_ms(uint32_t ms)
{
    while (ms--)
        delay_us(1000);
}
