// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// start.S - entry: cores 1-3 wait in the spin table, core 0 starts
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

.section .text.boot, "ax"
.global _start
.type _start, %function

.equ SPIN_TABLE_BASE, 0x00000000000000D8

_start:
    // core_id = MPIDR_EL1[1:0]
    mrs     x0, mpidr_el1
    and     x0, x0, #3
    cbz     x0, core0


    // secondary cores: wait for spin-table entry
    ldr     x1, =SPIN_TABLE_BASE
    lsl     x2, x0, #3          // core_id * 8
    add     x1, x1, x2          // x1 = &spin_cpu[core_id]

sec_wait:
    wfe
    ldr     x3, [x1]            // read start address (0 means "not yet")
    cbz     x3, sec_wait
    str     x3, [x1]            // clear mailbox by writing back
    dsb     sy
    isb
    br      x3                  // jump to entry (must set up stack there!)

core0:
    // Stack for core0
    ldr     x1, =__stack_top
    mov     sp, x1

    // clear .bss
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end

bss_loop:
    cmp     x0, x1
    b.hs    bss_done
    str     xzr, [x0], #8
    b       bss_loop

bss_done:
    bl      kernel_main

hang:
    wfe
    b       hang

.size _start, . - _start

