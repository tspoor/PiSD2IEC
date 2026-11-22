// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// vectors.S - exception vectors (for EL2 and EL1; exceptions_init() sets VBAR)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// For now: every exception saves the registers and calls
// exception_handler(type, regs), which prints a dump over the UART.

    .macro  ventry id
    .balign 0x80
    sub     sp, sp, #(34 * 8)
    stp     x0, x1, [sp, #0]
    mov     x0, #\id
    b       exc_common
    .endm

    .section ".text.vectors", "ax"
    .balign 0x800
    .globl  exception_vectors
exception_vectors:
    ventry  0       /* Sync   current EL, SP0 */
    ventry  1       /* IRQ    current EL, SP0 */
    ventry  2       /* FIQ    current EL, SP0 */
    ventry  3       /* SError current EL, SP0 */
    ventry  4       /* Sync   current EL, SPx */
    ventry  5       /* IRQ    current EL, SPx */
    ventry  6       /* FIQ    current EL, SPx */
    ventry  7       /* SError current EL, SPx */
    ventry  8       /* Sync   lower EL, AArch64 */
    ventry  9
    ventry  10
    ventry  11
    ventry  12      /* Sync   lower EL, AArch32 */
    ventry  13
    ventry  14
    ventry  15

exc_common:
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]
    mov     x1, sp
    bl      exception_handler
1:  wfe
    b       1b
