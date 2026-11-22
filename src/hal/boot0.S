// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// boot0.S - core 0's first step after start.S
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// start.S sets up the stack, clears .bss and calls kernel_main. That clear
// can end up in stale cache lines (see cache_inval.inc). So here, still
// without using the stack: invalidate L2 and L1, invalidate the I-cache,
// clear .bss again (now really in RAM) and go on to main_core0().
//
// L2 is shared by all cores, so this only happens here, before cores 1-3
// start.

#include "cache_inval.inc"

    .section ".text.boot0", "ax"
    .globl  kernel_main
kernel_main:
    DCACHE_INVAL_LEVEL 1
    DCACHE_INVAL_LEVEL 0
    ICACHE_INVAL

    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    stp     xzr, xzr, [x0], #16
    b       1b
2:  dsb     sy

    b       main_core0

    .ltorg
