// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// via6522.h - cycle-exact MOS 6522 VIA (the two I/O chips of the 1541)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Per clock cycle the machine first passes the CPU's access, if it is
// addressed (via_read/via_write), and then calls via_tick() once. Input
// lines (port pins, CA1/CA2/CB1/CB2) are set with the via_set_* functions;
// edges are detected there, as the real chip does.
//
// Timers, as in the datasheet's timing diagrams: after writing T1C-H (or
// T2C-H) with N, the counter reads N in the next cycle and then counts
// down once per cycle; one cycle after it reads 0 it reads $FFFF and the
// interrupt flag is set ("N + 1.5 cycles": the CPU sees the IRQ at the end
// of the cycle in which the counter goes from 0 to $FFFF). In free-run mode
// T1 reloads from the latch one cycle later, so the period is N + 2.
//
// Implemented: ports with DDR and input latching, T1 (one-shot, free-run,
// PB7 output), T2 (one-shot, PB6 pulse counting), CA1/CB1 edge interrupts,
// CA2/CB2 input edges and output modes (handshake, pulse, manual), IFR/IER.
// The shift register only stores its value: the 1541 does not use it.

#ifndef VIA6522_H
#define VIA6522_H

#include <stdint.h>

/* Interrupt flag bits */
#define VIA_IF_CA2  0x01
#define VIA_IF_CA1  0x02
#define VIA_IF_SR   0x04
#define VIA_IF_CB2  0x08
#define VIA_IF_CB1  0x10
#define VIA_IF_T2   0x20
#define VIA_IF_T1   0x40

struct via6522 {
    uint8_t  ora, orb, ddra, ddrb;
    uint8_t  acr, pcr, ifr, ier, sr;
    uint8_t  pa_in, pb_in;              /* levels on the pins from outside */
    uint8_t  pa_latch, pb_latch;        /* input latches (ACR bits 0/1) */
    uint8_t  ca1, ca2, cb1, cb2;        /* input levels */
    uint8_t  ca2_out, cb2_out;          /* output levels in output modes */
    uint8_t  ca2_pulse, cb2_pulse;      /* pulse mode: cycles left low */
    uint16_t t1c, t1l, t2c;
    uint8_t  t2l;                       /* T2 latch, low byte only */
    uint8_t  t1_skip, t1_reload, t1_armed, pb7;
    uint8_t  t2_skip, t2_armed;
};

static inline void via_reset(struct via6522 *v)
{
    /* Registers clear on reset, except the timers, their latches and SR */
    v->ora = v->orb = v->ddra = v->ddrb = 0;
    v->acr = v->pcr = v->ifr = v->ier = 0;
    v->ca2_out = v->cb2_out = 1;
    v->ca2_pulse = v->cb2_pulse = 0;
    v->t1_skip = v->t1_reload = v->t1_armed = 0;
    v->t2_skip = v->t2_armed = 0;
    v->pb7 = 1;
}

static inline int via_irq(const struct via6522 *v)
{
    return (v->ifr & v->ier & 0x7f) != 0;
}

/* CA2/CB2 control fields of the PCR */
static inline unsigned via_ca2_mode(const struct via6522 *v) { return (v->pcr >> 1) & 7; }
static inline unsigned via_cb2_mode(const struct via6522 *v) { return (v->pcr >> 5) & 7; }

/* Pin levels the VIA drives: outputs as ORx, inputs high (pull-ups) */
static inline uint8_t via_pa_out(const struct via6522 *v)
{
    return (uint8_t)((v->ora & v->ddra) | ~v->ddra);
}

static inline uint8_t via_pb_out(const struct via6522 *v)
{
    uint8_t pb = (uint8_t)((v->orb & v->ddrb) | ~v->ddrb);
    if (v->acr & 0x80)                  /* PB7 driven by T1 */
        pb = (uint8_t)((pb & 0x7f) | (v->pb7 << 7));
    return pb;
}

/* CA2/CB2 output level (1 when they are inputs) */
static inline int via_ca2_out(const struct via6522 *v)
{
    unsigned m = via_ca2_mode(v);
    if (m < 4)
        return 1;
    if (m == 6 || m == 7)
        return m == 7;
    return v->ca2_out && !v->ca2_pulse;
}

static inline int via_cb2_out(const struct via6522 *v)
{
    unsigned m = via_cb2_mode(v);
    if (m < 4)
        return 1;
    if (m == 6 || m == 7)
        return m == 7;
    return v->cb2_out && !v->cb2_pulse;
}

/* ----- input lines from the machine ----- */

static inline void via_set_ca1(struct via6522 *v, int level)
{
    level = !!level;
    if (level == v->ca1)
        return;
    v->ca1 = (uint8_t)level;
    if (level == (v->pcr & 0x01)) {     /* active edge */
        v->ifr |= VIA_IF_CA1;
        if (v->acr & 0x01)
            v->pa_latch = v->pa_in;
        if (via_ca2_mode(v) == 4)       /* handshake: CA2 back high */
            v->ca2_out = 1;
    }
}

static inline void via_set_cb1(struct via6522 *v, int level)
{
    level = !!level;
    if (level == v->cb1)
        return;
    v->cb1 = (uint8_t)level;
    if (level == ((v->pcr >> 4) & 0x01)) {
        v->ifr |= VIA_IF_CB1;
        if (v->acr & 0x02)
            v->pb_latch = via_pb_out(v) & v->pb_in;
        if (via_cb2_mode(v) == 4)
            v->cb2_out = 1;
    }
}

static inline void via_set_ca2(struct via6522 *v, int level)
{
    level = !!level;
    if (level == v->ca2)
        return;
    v->ca2 = (uint8_t)level;
    unsigned m = via_ca2_mode(v);
    if (m < 4 && (unsigned)level == ((m >> 1) & 1))
        v->ifr |= VIA_IF_CA2;
}

static inline void via_set_cb2(struct via6522 *v, int level)
{
    level = !!level;
    if (level == v->cb2)
        return;
    v->cb2 = (uint8_t)level;
    unsigned m = via_cb2_mode(v);
    if (m < 4 && (unsigned)level == ((m >> 1) & 1))
        v->ifr |= VIA_IF_CB2;
}

static inline void via_set_pa(struct via6522 *v, uint8_t pins) { v->pa_in = pins; }

static inline void via_set_pb(struct via6522 *v, uint8_t pins)
{
    /* T2 in pulse counting mode counts falling edges on PB6 */
    if ((v->acr & 0x20) && (v->pb_in & 0x40) && !(pins & 0x40)) {
        if (v->t2c == 0 && v->t2_armed) {
            v->ifr |= VIA_IF_T2;
            v->t2_armed = 0;
        }
        v->t2c--;
    }
    v->pb_in = pins;
}

/* ----- register access by the CPU ----- */

/* Port A/B access clears CA1/CB1 and, unless "independent", CA2/CB2 */
static inline void via_port_a_access(struct via6522 *v)
{
    unsigned m = via_ca2_mode(v);
    v->ifr &= (uint8_t)~VIA_IF_CA1;
    if (!(m == 1 || m == 3))
        v->ifr &= (uint8_t)~VIA_IF_CA2;
    if (m == 4)
        v->ca2_out = 0;                 /* handshake: low until CA1 edge */
    else if (m == 5)
        v->ca2_pulse = 2;               /* pulse: low during the next cycle */
}

static inline void via_port_b_access(struct via6522 *v, int write)
{
    unsigned m = via_cb2_mode(v);
    v->ifr &= (uint8_t)~VIA_IF_CB1;
    if (!(m == 1 || m == 3))
        v->ifr &= (uint8_t)~VIA_IF_CB2;
    if (write && m == 4)                /* CB2 handshake only on writes */
        v->cb2_out = 0;
    else if (write && m == 5)
        v->cb2_pulse = 2;
}

static inline uint8_t via_read(struct via6522 *v, unsigned reg)
{
    switch (reg & 15) {
    case 0x0: {                         /* IRB: outputs from ORB, inputs from pins */
        via_port_b_access(v, 0);
        uint8_t in = (v->acr & 0x02) ? v->pb_latch : (uint8_t)(v->pb_in & via_pb_out(v));
        uint8_t r = (uint8_t)((v->orb & v->ddrb) | (in & ~v->ddrb));
        if (v->acr & 0x80)
            r = (uint8_t)((r & 0x7f) | (v->pb7 << 7));
        return r;
    }
    case 0x1:                           /* IRA: the pins (with handshake) */
        via_port_a_access(v);
        /* fall through */
    case 0xf:
        if (v->acr & 0x01)
            return v->pa_latch;
        return (uint8_t)(v->pa_in & via_pa_out(v));
    case 0x2: return v->ddrb;
    case 0x3: return v->ddra;
    case 0x4: v->ifr &= (uint8_t)~VIA_IF_T1; return (uint8_t)v->t1c;
    case 0x5: return (uint8_t)(v->t1c >> 8);
    case 0x6: return (uint8_t)v->t1l;
    case 0x7: return (uint8_t)(v->t1l >> 8);
    case 0x8: v->ifr &= (uint8_t)~VIA_IF_T2; return (uint8_t)v->t2c;
    case 0x9: return (uint8_t)(v->t2c >> 8);
    case 0xa: v->ifr &= (uint8_t)~VIA_IF_SR; return v->sr;
    case 0xb: return v->acr;
    case 0xc: return v->pcr;
    case 0xd: return (uint8_t)(v->ifr | (via_irq(v) ? 0x80 : 0));
    default:  return (uint8_t)(v->ier | 0x80);
    }
}

static inline void via_write(struct via6522 *v, unsigned reg, uint8_t val)
{
    switch (reg & 15) {
    case 0x0: v->orb = val; via_port_b_access(v, 1); break;
    case 0x1: v->ora = val; via_port_a_access(v); break;
    case 0xf: v->ora = val; break;
    case 0x2: v->ddrb = val; break;
    case 0x3: v->ddra = val; break;
    case 0x4:
    case 0x6: v->t1l = (uint16_t)((v->t1l & 0xff00) | val); break;
    case 0x5:                           /* T1C-H: load and start */
        v->t1l = (uint16_t)((v->t1l & 0x00ff) | val << 8);
        v->t1c = v->t1l;
        v->ifr &= (uint8_t)~VIA_IF_T1;
        v->t1_armed = 1;
        v->t1_skip = 1;
        v->t1_reload = 0;
        if (v->acr & 0x80)
            v->pb7 = 0;
        break;
    case 0x7:                           /* T1L-H: latch only, clears the flag */
        v->t1l = (uint16_t)((v->t1l & 0x00ff) | val << 8);
        v->ifr &= (uint8_t)~VIA_IF_T1;
        break;
    case 0x8: v->t2l = val; break;
    case 0x9:                           /* T2C-H: load and start */
        v->t2c = (uint16_t)(v->t2l | val << 8);
        v->ifr &= (uint8_t)~VIA_IF_T2;
        v->t2_armed = 1;
        v->t2_skip = 1;
        break;
    case 0xa: v->sr = val; v->ifr &= (uint8_t)~VIA_IF_SR; break;
    case 0xb: v->acr = val; break;
    case 0xc: v->pcr = val; break;
    case 0xd: v->ifr &= (uint8_t)~(val & 0x7f); break;
    default:
        if (val & 0x80)
            v->ier |= val & 0x7f;
        else
            v->ier &= (uint8_t)~(val & 0x7f);
        break;
    }
}

/* ----- one clock cycle (after the CPU's access in that cycle) ----- */

static inline void via_tick(struct via6522 *v)
{
    /* Timer 1 */
    if (v->t1_skip) {
        v->t1_skip = 0;
    } else if (v->t1_reload) {
        v->t1c = v->t1l;
        v->t1_reload = 0;
    } else if (v->t1c == 0) {
        v->t1c = 0xffff;
        if (v->t1_armed) {
            v->ifr |= VIA_IF_T1;
            if (v->acr & 0x40) {
                v->pb7 ^= 1;            /* free-run: square wave on PB7 */
            } else {
                v->t1_armed = 0;
                v->pb7 = 1;             /* one-shot: PB7 back high */
            }
        }
        if (v->acr & 0x40)
            v->t1_reload = 1;
    } else {
        v->t1c--;
    }

    /* Timer 2 (timed mode; pulse counting happens in via_set_pb) */
    if (!(v->acr & 0x20)) {
        if (v->t2_skip) {
            v->t2_skip = 0;
        } else {
            if (v->t2c == 0 && v->t2_armed) {
                v->ifr |= VIA_IF_T2;
                v->t2_armed = 0;
            }
            v->t2c--;
        }
    }

    /* Pulse output modes: CA2/CB2 low for one cycle */
    if (v->ca2_pulse)
        v->ca2_pulse--;
    if (v->cb2_pulse)
        v->cb2_pulse--;
}

#endif
