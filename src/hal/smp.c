// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// smp.c - start cores 1-3 through the firmware's spin table
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// The firmware's armstub keeps cores 1-3 waiting (wfe) until there is a
// start address in their slot: 0xE0 (core 1), 0xE8 (core 2), 0xF0 (core 3).
// Those cores read the slot with MMU and caches off, so core 0 has to write
// the cache line back before it wakes them (sev).

#include <stdint.h>
#include "rpi.h"
#include "mmu.h"
#include "timer.h"
#include "smp.h"

#define SPIN_TABLE_BASE 0xD8u           /* slot of core n: 0xD8 + 8*n */

extern void secondary_start(void);      /* smp_entry.S */
void secondary_main(unsigned core);

static void (*volatile core_entry[4])(void);
static uint32_t core_alive[4];

static volatile uint64_t *spin_slot(unsigned core)
{
    uintptr_t a = SPIN_TABLE_BASE + 8u * core;
    __asm__("" : "+r"(a));              /* avoids GCC's "null pointer" warning */
    return (volatile uint64_t *)a;
}

int smp_start_core(unsigned core, void (*fn)(void))
{
    if (core < 1 || core > 3 || !fn)
        return 0;

    core_entry[core] = fn;              /* the core only reads this with caches on */
    __atomic_store_n(&core_alive[core], 0, __ATOMIC_RELEASE);

    volatile uint64_t *slot = spin_slot(core);
    *slot = (uint64_t)(uintptr_t)secondary_start;
    dcache_clean_inval_range(slot, sizeof(*slot));
    __asm__ volatile("dsb sy\n sev" ::: "memory");

    uint64_t end = timer_ticks() + timer_us_to_ticks(100000);
    while (timer_ticks() < end)
        if (__atomic_load_n(&core_alive[core], __ATOMIC_ACQUIRE))
            return 1;
    return 0;
}

int smp_core_running(unsigned core)
{
    return core < 4 && (core == 0 || __atomic_load_n(&core_alive[core], __ATOMIC_ACQUIRE));
}

/* smp_entry.S jumps here, with MMU and caches on */
void secondary_main(unsigned core)
{
    __atomic_store_n(&core_alive[core], 1, __ATOMIC_RELEASE);
    core_entry[core]();
}
