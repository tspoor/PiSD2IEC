// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// arch-timer.h - timers for sd2iec on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// delay_us/delay_ms are the same functions as in PiSD2IEC's hal/timer.c
// (same name and ABI). The time-out uses the ARM counter (CNTPCT).

#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <stdint.h>

/* Types for tick values (100 Hz) */
typedef uint32_t tick_t;
typedef int32_t  stick_t;

void delay_us(unsigned int time);
void delay_ms(unsigned int time);

/* One time-out at a time, per core (only used by the IEC core) */
void         start_timeout(unsigned int usecs);
unsigned int has_timed_out(void);

#endif
