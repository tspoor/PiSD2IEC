// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// power.c - reboot through the BCM2710's PM watchdog (like Linux bcm2835_wdt)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "rpi.h"
#include "power.h"

#define PM_BASE         (PERIPH_BASE + 0x100000)
#define PM_RSTC         REG32(PM_BASE + 0x1C)
#define PM_WDOG         REG32(PM_BASE + 0x24)

#define PM_PASSWORD     0x5A000000u
#define PM_RSTC_WRCFG_CLR        0xFFFFFFCFu
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u

void pi_reboot(void)
{
    __asm__ volatile("dsb sy" ::: "memory");

    PM_WDOG = PM_PASSWORD | 10;         /* time-out: 10 ticks (~150 us) */
    PM_RSTC = PM_PASSWORD | (PM_RSTC & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET;

    for (;;)
        __asm__ volatile("wfe");
}
