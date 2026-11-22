// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// arch-timer.c - time-out for sd2iec on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// delay_us/delay_ms come from PiSD2IEC's hal/timer.c (same ABI).

#include <stdint.h>
#include "hal/timer.h"
#include "arch-timer.h"

static uint64_t timeout_end;

void start_timeout(unsigned int usecs)
{
    timeout_end = timer_ticks() + timer_us_to_ticks(usecs);
}

unsigned int has_timed_out(void)
{
    return timer_ticks() >= timeout_end;
}
