// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// cpu6502.h - cycle-exact NMOS 6502 (the CPU of the 1541)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// The CPU itself is in cpu6502_impl.h, which a machine includes after
// defining its bus (see there). This header holds the state and the API.
//
// Cycle exactness: every clock cycle of the real 6502 is exactly one bus
// access here, including the dummy reads and writes the real chip does
// (they matter: reading a VIA register clears flags, writing it starts
// timers). The machine advances its other chips once per access, so every
// side effect lands on the right cycle.

#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

#define P_C 0x01                        /* carry */
#define P_Z 0x02                        /* zero */
#define P_I 0x04                        /* IRQ disable */
#define P_D 0x08                        /* decimal */
#define P_B 0x10                        /* break (only on the stack) */
#define P_U 0x20                        /* unused, always 1 on the stack */
#define P_V 0x40                        /* overflow (also set by the SO pin) */
#define P_N 0x80                        /* negative */

struct cpu6502 {
    uint16_t pc;
    uint8_t  a, x, y, s, p;
    uint8_t  jammed;                    /* a JAM opcode stopped the CPU */
    uint8_t  irq_now;                   /* IRQ asserted and enabled, after the last cycle */
    uint8_t  irq_prev;                  /* ... after the cycle before that */
    uint8_t  irq_take;                  /* take the IRQ before the next instruction */
    uint64_t cycles;                    /* clock cycles since reset */
};

/* SO pin: a falling edge sets the V flag (the 1541's BYTE READY) */
static inline void cpu6502_set_overflow(struct cpu6502 *c)
{
    c->p |= P_V;
}

#endif
