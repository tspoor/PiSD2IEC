// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// mmu.h - MMU, caches and cache maintenance
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef MMU_H
#define MMU_H

#include <stddef.h>

void mmu_init(void);       /* core 0: build the tables and turn on the MMU */
void mmu_enable(void);     /* cores 1-3: turn on the MMU with the same tables */

/* Write dirty cache lines back and invalidate them (for DMA/mailbox). */
void dcache_clean_inval_range(const volatile void *addr, size_t len);

#endif
