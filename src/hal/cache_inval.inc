// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// cache_inval.inc - invalidate caches without a stack (for startup assembly)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// After a reset the cache can still hold lines from the previous run. Even
// with caches "off" the CPU still hits those lines: writes end up there
// instead of in RAM, and reads return the old value. So invalidate first
// (discard, don't write back), before anything else touches memory.
//
// Uses x0-x8. Level 0 = L1 (per core), 1 = L2 (shared by all cores).

    .macro  DCACHE_INVAL_LEVEL level
    mov     x0, #(\level << 1)
    msr     csselr_el1, x0
    isb
    mrs     x1, ccsidr_el1
    and     x2, x1, #7
    add     x2, x2, #4                  /* log2(line size in bytes) */
    ubfx    x3, x1, #3, #10             /* number of ways - 1 */
    ubfx    x4, x1, #13, #15            /* number of sets - 1 */
    clz     w5, w3                      /* position of the way field */
1:  mov     x6, x4
2:  lsl     x7, x3, x5
    lsl     x8, x6, x2
    orr     x7, x7, x8
    orr     x7, x7, x0
    dc      isw, x7
    subs    x6, x6, #1
    b.ge    2b
    subs    x3, x3, #1
    b.ge    1b
    dsb     sy
    .endm

    .macro  ICACHE_INVAL
    ic      iallu
    dsb     sy
    isb
    .endm
