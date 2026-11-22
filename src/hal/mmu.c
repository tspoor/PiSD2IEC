// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// mmu.c - identity mapping with 2 MB blocks
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
//   0x00000000-0x1FFFFFFF  RAM, normal memory, write-back cached
//   0x20000000-0x3EFFFFFF  not mapped
//   0x3F000000-0x3FFFFFFF  peripherals (device-nGnRnE)
//   0x40000000-0x7FFFFFFF  local peripherals (device, 1 GB block)
//
// Works in EL2 (where start.S leaves us) and in EL1. In EL2 the _EL2
// registers apply, AP[1] in the descriptors is RES1 and bit 54 is XN.

#include <stdint.h>
#include "rpi.h"
#include "mmu.h"

#define PT_VALID        (1ul << 0)
#define PT_TABLE        (1ul << 1)
#define PT_ATTR(n)      ((uint64_t)(n) << 2)
#define PT_SH_INNER     (3ul << 8)
#define PT_AF           (1ul << 10)
#define PT_AP1          (1ul << 6)      /* EL2: RES1 */
#define PT_PXN          (1ul << 53)     /* EL1 */
#define PT_UXN          (1ul << 54)     /* EL1: UXN, EL2: XN */

#define ATTR_NORMAL     0               /* MAIR index */
#define ATTR_DEVICE     1

#define CACHE_LINE      64

static uint64_t l1_table[512] __attribute__((aligned(4096)));
static uint64_t l2_table[512] __attribute__((aligned(4096)));

static uint64_t mair_val, tcr_val;
static int el2;                         /* tables built for EL2 */

/*
 * Builds the tables (core 0 only, with MMU and caches still off, so they
 * go straight to RAM) and then turns on core 0's MMU.
 */
void mmu_init(void)
{
    el2 = current_el() == 2;
    const uint64_t ap  = el2 ? PT_AP1 : 0;
    const uint64_t dev_xn = el2 ? PT_UXN : (PT_PXN | PT_UXN);
    const uint64_t normal = PT_VALID | PT_AF | ap | PT_SH_INNER | PT_ATTR(ATTR_NORMAL);
    const uint64_t device = PT_VALID | PT_AF | ap | PT_ATTR(ATTR_DEVICE) | dev_xn;

    for (unsigned i = 0; i < 512; i++) {
        uint64_t pa = (uint64_t)i << 21;
        if (pa < RAM_END)
            l2_table[i] = pa | normal;
        else if (pa >= PERIPH_BASE)
            l2_table[i] = pa | device;
        else
            l2_table[i] = 0;
    }
    l1_table[0] = (uint64_t)(uintptr_t)l2_table | PT_VALID | PT_TABLE;
    l1_table[1] = LOCAL_BASE | device;

    mair_val      = (0xFFul << (8 * ATTR_NORMAL)) |     /* normal, WB RW-alloc */
                    (0x00ul << (8 * ATTR_DEVICE));      /* device-nGnRnE */
    tcr_val       = 32ul            /* T0SZ: 4 GB address space, starts at level 1 */
                  | (1ul << 8)      /* IRGN0: WB WA */
                  | (1ul << 10)     /* ORGN0: WB WA */
                  | (3ul << 12)     /* SH0: inner shareable */
                  | (0ul << 14)     /* TG0: 4 KB pages */
                  | (1ul << 23);    /* EL1: EPD1 (no TTBR1); EL2: RES1 */
                                    /* (I)PS = 0: 32-bit physical addresses */
    if (el2)
        tcr_val |= 1ul << 31;       /* EL2: RES1 */

    /* The other cores read these values with MMU and caches still off. */
    dcache_clean_inval_range(&mair_val, sizeof(mair_val));
    dcache_clean_inval_range(&tcr_val, sizeof(tcr_val));
    dcache_clean_inval_range(&el2, sizeof(el2));

    mmu_enable();
}

/*
 * Turns on MMU and caches with the tables from mmu_init(). Each core calls
 * this itself; cores 1-3 from smp_entry.S, before their caches are valid.
 * This is a leaf function that doesn't use the stack, so nothing on the
 * stack changes meaning while the cache is being turned on.
 */
void mmu_enable(void)
{
    uint64_t sctlr;

    /* The caches have already been invalidated: core 0 in boot0.S (L2 and
       L1), cores 1-3 in smp_entry.S (their own L1). */

    if (el2) {
        __asm__ volatile(
            "msr mair_el2, %0\n"
            "msr tcr_el2, %1\n"
            "msr ttbr0_el2, %2\n"
            "dsb ish\n"
            "isb\n"
            "tlbi alle2\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(mair_val), "r"(tcr_val), "r"((uint64_t)(uintptr_t)l1_table) : "memory");
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    } else {
        __asm__ volatile(
            "msr mair_el1, %0\n"
            "msr tcr_el1, %1\n"
            "msr ttbr0_el1, %2\n"
            "dsb ish\n"
            "isb\n"
            "tlbi vmalle1\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(mair_val), "r"(tcr_val), "r"((uint64_t)(uintptr_t)l1_table) : "memory");
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    }

    sctlr |=  (1ul << 0) | (1ul << 2) | (1ul << 12);   /* M, C, I */
    sctlr &= ~((1ul << 1) | (1ul << 19));              /* A, WXN off */

    if (el2)
        __asm__ volatile("msr sctlr_el2, %0\n isb" :: "r"(sctlr) : "memory");
    else
        __asm__ volatile("msr sctlr_el1, %0\n isb" :: "r"(sctlr) : "memory");
}

void dcache_clean_inval_range(const volatile void *addr, size_t len)
{
    uintptr_t a   = (uintptr_t)addr & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)addr + len;
    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
