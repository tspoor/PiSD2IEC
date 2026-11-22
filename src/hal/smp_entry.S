// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// smp_entry.S - entry point for cores 1-3
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// smp_start_core() puts the address of secondary_start in the core's spin
// table slot. The firmware's armstub then jumps here, in EL2 with MMU and
// caches off, just like core 0 in start.S.
//
// Per core: invalidate its own L1 and I-cache, own stack, vector table,
// MMU and caches on (same tables as core 0), then secondary_main(core)
// in smp.c.

#include "cache_inval.inc"

#define CORE_STACK_SIZE 0x4000          /* see linker.ld */

    .section ".text.secondary", "ax"
    .globl  secondary_start
secondary_start:
    DCACHE_INVAL_LEVEL 0                /* own L1; core 0 already did L2 */
    ICACHE_INVAL

    mrs     x9, mpidr_el1
    and     x9, x9, #3
    add     x9, x9, #1                  /* stack of core n: top of block n */
    ldr     x10, =__stack_start
    mov     x11, #CORE_STACK_SIZE
    madd    x10, x9, x11, x10
    mov     sp, x10

    bl      exceptions_init
    bl      mmu_enable                  /* leaf function: doesn't use the stack */

    mrs     x0, mpidr_el1
    and     x0, x0, #3
    bl      secondary_main

1:  wfe
    b       1b

    .ltorg
