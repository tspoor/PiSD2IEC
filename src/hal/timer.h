// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// timer.h - timekeeping with the ARM generic timer (CNTPCT_EL0, 19.2 MHz)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

static inline uint64_t timer_ticks(void)
{
    uint64_t v;
    __asm__ volatile("isb\n mrs %0, cntpct_el0" : "=r"(v) :: "memory");
    return v;
}

void     timer_init(void);
uint64_t timer_freq(void);
uint64_t timer_us(void);                 /* microseconds since startup */
uint64_t timer_us_to_ticks(uint32_t us);
void     delay_us(uint32_t us);
void     delay_ms(uint32_t ms);

#endif
