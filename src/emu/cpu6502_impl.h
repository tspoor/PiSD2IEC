// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// cpu6502_impl.h - cycle-exact NMOS 6502, included by a machine
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Before including this file, the machine defines:
//
//   static inline uint8_t cpu_bus_read(uint16_t addr);           one cycle
//   static inline void    cpu_bus_write(uint16_t addr, uint8_t v); one cycle
//   static inline int     cpu_irq_line(void);    IRQ input asserted (level)
//
// Each call of cpu_bus_read/cpu_bus_write is one clock cycle, in the order
// and with the addresses of the real NMOS 6502 (as documented in "64doc"
// and verified against per-cycle test suites), including dummy accesses.
// The machine runs its other chips once per call. The SO pin (the 1541's
// BYTE READY) is the machine setting P_V in cpu->p (cpu6502_set_overflow).
//
// Interrupts: like the real chip, the IRQ input is looked at during the
// last cycle of an instruction, with the state of the cycle before
// (irq_prev), so an IRQ that arrives in the last cycle waits one more
// instruction, CLI/SEI/PLP act one instruction late and RTI immediately.
// A taken branch that does not cross a page polls one cycle early.
//
// All undocumented opcodes are implemented, as most drive fast loaders
// and copy protections rely on some of them. JAM opcodes stop the CPU.
//
// Functions (static, one copy per machine):
//   cpu6502_reset(c)      reset sequence (7 cycles), PC from $FFFC
//   cpu6502_step(c)       one instruction, or the IRQ sequence
//   cpu6502_set_overflow(c)  SO pin edge (in cpu6502.h)

#include "cpu6502.h"

#ifndef CPU6502_ANE_MAGIC
#define CPU6502_ANE_MAGIC 0xEE          /* "magic" constant of ANE/LXA */
#endif

/* ----- bus access: one cycle each, then sample the IRQ input ----- */

static inline void cpu_poll(struct cpu6502 *c)
{
    c->cycles++;
    c->irq_prev = c->irq_now;
    c->irq_now  = cpu_irq_line() && !(c->p & P_I);
}

static inline uint8_t rd(struct cpu6502 *c, uint16_t a)
{
    uint8_t v = cpu_bus_read(a);
    cpu_poll(c);
    return v;
}

static inline void wr(struct cpu6502 *c, uint16_t a, uint8_t v)
{
    cpu_bus_write(a, v);
    cpu_poll(c);
}

static inline uint8_t fetch(struct cpu6502 *c)
{
    return rd(c, c->pc++);
}

static inline void push(struct cpu6502 *c, uint8_t v)
{
    wr(c, 0x100 | c->s, v);
    c->s--;
}

static inline uint8_t pull(struct cpu6502 *c)
{
    c->s++;
    return rd(c, 0x100 | c->s);
}

static inline void set_nz(struct cpu6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(P_N | P_Z)) | (v & P_N) | (v ? 0 : P_Z));
}

static inline void set_flag(struct cpu6502 *c, uint8_t f, int on)
{
    c->p = (uint8_t)(on ? (c->p | f) : (c->p & ~f));
}

/* ----- addressing modes: return the effective address ----- */

static inline uint16_t am_zp(struct cpu6502 *c)
{
    return fetch(c);
}

static inline uint16_t am_zpi(struct cpu6502 *c, uint8_t idx)
{
    uint8_t a = fetch(c);
    rd(c, a);                           /* dummy read before indexing */
    return (uint8_t)(a + idx);
}

static inline uint16_t am_abs(struct cpu6502 *c)
{
    uint16_t lo = fetch(c);
    return (uint16_t)(lo | (fetch(c) << 8));
}

/* abs,X / abs,Y for reads: extra (dummy) cycle only on a page crossing */
static inline uint16_t am_absi_r(struct cpu6502 *c, uint8_t idx)
{
    uint16_t lo = fetch(c), hi = fetch(c);
    uint16_t ea = (uint16_t)((hi << 8 | lo) + idx);
    if (lo + idx > 0xff)
        rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + idx)));
    return ea;
}

/* abs,X / abs,Y for writes and read-modify-write: always the dummy cycle */
static inline uint16_t am_absi_w(struct cpu6502 *c, uint8_t idx)
{
    uint16_t lo = fetch(c), hi = fetch(c);
    rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + idx)));
    return (uint16_t)((hi << 8 | lo) + idx);
}

static inline uint16_t am_indx(struct cpu6502 *c)
{
    uint8_t p = fetch(c);
    rd(c, p);                           /* dummy read before adding X */
    p = (uint8_t)(p + c->x);
    uint16_t lo = rd(c, p);
    return (uint16_t)(lo | rd(c, (uint8_t)(p + 1)) << 8);
}

static inline uint16_t am_indy_r(struct cpu6502 *c)
{
    uint8_t p = fetch(c);
    uint16_t lo = rd(c, p), hi = rd(c, (uint8_t)(p + 1));
    uint16_t ea = (uint16_t)((hi << 8 | lo) + c->y);
    if (lo + c->y > 0xff)
        rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + c->y)));
    return ea;
}

static inline uint16_t am_indy_w(struct cpu6502 *c)
{
    uint8_t p = fetch(c);
    uint16_t lo = rd(c, p), hi = rd(c, (uint8_t)(p + 1));
    rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + c->y)));
    return (uint16_t)((hi << 8 | lo) + c->y);
}

/* ----- ALU ----- */

static inline void op_adc(struct cpu6502 *c, uint8_t v)
{
    unsigned a = c->a, cin = c->p & P_C;

    if (c->p & P_D) {
        /* NMOS decimal mode: Z from the binary sum, N and V from the
           intermediate result, C from the decimal result */
        unsigned t = (a & 0x0f) + (v & 0x0f) + cin;
        if (t > 0x09)
            t += 0x06;
        if (t <= 0x0f)
            t = (t & 0x0f) + (a & 0xf0) + (v & 0xf0);
        else
            t = (t & 0x0f) + (a & 0xf0) + (v & 0xf0) + 0x10;
        set_flag(c, P_Z, !((a + v + cin) & 0xff));
        set_flag(c, P_N, t & 0x80);
        set_flag(c, P_V, ((a ^ t) & 0x80) && !((a ^ v) & 0x80));
        if ((t & 0x1f0) > 0x90)
            t += 0x60;
        set_flag(c, P_C, (t & 0xff0) > 0xf0);
        c->a = (uint8_t)t;
    } else {
        unsigned t = a + v + cin;
        set_flag(c, P_C, t > 0xff);
        set_flag(c, P_V, !((a ^ v) & 0x80) && ((a ^ t) & 0x80));
        c->a = (uint8_t)t;
        set_nz(c, c->a);
    }
}

static inline void op_sbc(struct cpu6502 *c, uint8_t v)
{
    unsigned a = c->a, borrow = (c->p & P_C) ? 0 : 1;
    unsigned t = a - v - borrow;

    set_flag(c, P_C, t < 0x100);
    set_flag(c, P_V, ((a ^ t) & 0x80) && ((a ^ v) & 0x80));
    set_nz(c, (uint8_t)t);

    if (c->p & P_D) {
        /* NMOS decimal mode: flags as in binary mode, result adjusted */
        unsigned lo = (a & 0x0f) - (v & 0x0f) - borrow;
        if (lo & 0x10)
            lo = ((lo - 6) & 0x0f) | ((a & 0xf0) - (v & 0xf0) - 0x10);
        else
            lo = (lo & 0x0f) | ((a & 0xf0) - (v & 0xf0));
        if (lo & 0x100)
            lo -= 0x60;
        c->a = (uint8_t)lo;
    } else {
        c->a = (uint8_t)t;
    }
}

static inline void op_cmp(struct cpu6502 *c, uint8_t r, uint8_t v)
{
    unsigned t = (unsigned)r - v;
    set_flag(c, P_C, r >= v);
    set_nz(c, (uint8_t)t);
}

static inline uint8_t op_asl(struct cpu6502 *c, uint8_t v)
{
    set_flag(c, P_C, v & 0x80);
    v = (uint8_t)(v << 1);
    set_nz(c, v);
    return v;
}

static inline uint8_t op_lsr(struct cpu6502 *c, uint8_t v)
{
    set_flag(c, P_C, v & 0x01);
    v >>= 1;
    set_nz(c, v);
    return v;
}

static inline uint8_t op_rol(struct cpu6502 *c, uint8_t v)
{
    unsigned cin = c->p & P_C;
    set_flag(c, P_C, v & 0x80);
    v = (uint8_t)(v << 1 | cin);
    set_nz(c, v);
    return v;
}

static inline uint8_t op_ror(struct cpu6502 *c, uint8_t v)
{
    unsigned cin = c->p & P_C;
    set_flag(c, P_C, v & 0x01);
    v = (uint8_t)(v >> 1 | cin << 7);
    set_nz(c, v);
    return v;
}

static inline void op_bit(struct cpu6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(P_N | P_V | P_Z)) | (v & (P_N | P_V)) |
                     ((c->a & v) ? 0 : P_Z));
}

/* ARR: AND, then ROR A with odd flags; decimal mode adjusts as well */
static inline void op_arr(struct cpu6502 *c, uint8_t v)
{
    uint8_t t = c->a & v;
    uint8_t r = (uint8_t)(t >> 1 | (c->p & P_C) << 7);

    if (c->p & P_D) {
        set_flag(c, P_N, c->p & P_C);
        set_flag(c, P_Z, !r);
        set_flag(c, P_V, (t ^ r) & 0x40);
        if ((t & 0x0f) + (t & 0x01) > 0x05)
            r = (uint8_t)((r & 0xf0) | ((r + 0x06) & 0x0f));
        if ((t & 0xf0) + (t & 0x10) > 0x50) {
            r = (uint8_t)(r + 0x60);
            c->p |= P_C;
        } else {
            c->p &= (uint8_t)~P_C;
        }
    } else {
        set_nz(c, r);
        set_flag(c, P_C, r & 0x40);
        set_flag(c, P_V, ((r >> 6) ^ (r >> 5)) & 1);
    }
    c->a = r;
}

/* ----- interrupt and reset sequences ----- */

static void cpu6502_irq(struct cpu6502 *c)
{
    rd(c, c->pc);                       /* two dummy cycles */
    rd(c, c->pc);
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((c->p & ~P_B) | P_U));
    c->p |= P_I;
    uint16_t lo = rd(c, 0xfffe);
    c->pc = (uint16_t)(lo | rd(c, 0xffff) << 8);
}

static __attribute__((unused)) void cpu6502_reset(struct cpu6502 *c)
{
    c->jammed = 0;
    c->irq_now = c->irq_prev = c->irq_take = 0;
    rd(c, c->pc);
    rd(c, c->pc);
    rd(c, 0x100 | c->s);                /* three "pushes" that only read */
    c->s--;
    rd(c, 0x100 | c->s);
    c->s--;
    rd(c, 0x100 | c->s);
    c->s--;
    c->p |= P_I | P_U;
    uint16_t lo = rd(c, 0xfffc);
    c->pc = (uint16_t)(lo | rd(c, 0xfffd) << 8);
}

/* ----- one instruction ----- */

/* Read instructions: operand from memory or immediate */
#define READ_OP(code, mode_ea, body)                       \
    case code: { uint8_t v = rd(c, mode_ea); body; break; }
#define IMM_OP(code, body)                                 \
    case code: { uint8_t v = fetch(c); body; break; }
#define WRITE_OP(code, mode_ea, value)                     \
    case code: { uint16_t ea = mode_ea; wr(c, ea, value); break; }
/* Read-modify-write: read, write the old value back, write the new one */
#define RMW_OP(code, mode_ea, body)                        \
    case code: { uint16_t ea = mode_ea; uint8_t v = rd(c, ea); \
                 wr(c, ea, v); body; wr(c, ea, v); break; }
/* Implied: one dummy read of the next byte */
#define IMPL_OP(code, body)                                \
    case code: { rd(c, c->pc); body; break; }

/* The usual eight addressing modes of the ALU group */
#define ALU_READ(base, body)                               \
    IMM_OP (base + 0x09, body)                             \
    READ_OP(base + 0x05, am_zp(c), body)                   \
    READ_OP(base + 0x15, am_zpi(c, c->x), body)            \
    READ_OP(base + 0x0d, am_abs(c), body)                  \
    READ_OP(base + 0x1d, am_absi_r(c, c->x), body)         \
    READ_OP(base + 0x19, am_absi_r(c, c->y), body)         \
    READ_OP(base + 0x01, am_indx(c), body)                 \
    READ_OP(base + 0x11, am_indy_r(c), body)

/* Shifts and INC/DEC: zp, zp,X, abs, abs,X */
#define RMW_GROUP(base, body)                              \
    RMW_OP(base + 0x06, am_zp(c), body)                    \
    RMW_OP(base + 0x16, am_zpi(c, c->x), body)             \
    RMW_OP(base + 0x0e, am_abs(c), body)                   \
    RMW_OP(base + 0x1e, am_absi_w(c, c->x), body)

/* Undocumented RMW+ALU combos: all seven modes */
#define RMW_ILLEGAL(base, body)                            \
    RMW_OP(base + 0x07, am_zp(c), body)                    \
    RMW_OP(base + 0x17, am_zpi(c, c->x), body)             \
    RMW_OP(base + 0x0f, am_abs(c), body)                   \
    RMW_OP(base + 0x1f, am_absi_w(c, c->x), body)          \
    RMW_OP(base + 0x1b, am_absi_w(c, c->y), body)          \
    RMW_OP(base + 0x03, am_indx(c), body)                  \
    RMW_OP(base + 0x13, am_indy_w(c), body)

/* SHA/SHX/SHY/TAS: store reg & (high byte + 1); on a page crossing the
   stored value also replaces the high byte of the address */
static inline void store_and_high(struct cpu6502 *c, uint16_t lo, uint16_t hi,
                                  uint8_t idx, uint8_t reg)
{
    rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + idx)));
    uint16_t ea = (uint16_t)((hi << 8 | lo) + idx);
    uint8_t v = reg & (uint8_t)(hi + 1);
    if (lo + idx > 0xff)
        ea = (uint16_t)(v << 8 | (ea & 0xff));
    wr(c, ea, v);
}

static inline void branch(struct cpu6502 *c, int cond)
{
    int8_t off = (int8_t)fetch(c);
    if (!cond)
        return;
    uint8_t early = c->irq_prev;        /* poll as if a 2-cycle instruction */
    rd(c, c->pc);
    uint16_t to = (uint16_t)(c->pc + off);
    if ((to ^ c->pc) & 0xff00) {
        rd(c, (uint16_t)((c->pc & 0xff00) | (to & 0xff)));
        c->pc = to;
        return;
    }
    c->pc = to;
    c->irq_prev = early;
}

static void cpu6502_step(struct cpu6502 *c)
{
    if (c->jammed) {
        rd(c, 0xffff);
        return;
    }
    if (c->irq_take) {
        c->irq_take = 0;
        cpu6502_irq(c);
        c->irq_take = c->irq_prev;
        return;
    }

    uint8_t op = fetch(c);

    switch (op) {
    /* --- loads, stores, ALU --- */
    ALU_READ(0x00, { c->a |= v; set_nz(c, c->a); })                 /* ORA */
    ALU_READ(0x20, { c->a &= v; set_nz(c, c->a); })                 /* AND */
    ALU_READ(0x40, { c->a ^= v; set_nz(c, c->a); })                 /* EOR */
    ALU_READ(0x60, { op_adc(c, v); })                               /* ADC */
    ALU_READ(0xa0, { c->a = v; set_nz(c, v); })                     /* LDA */
    ALU_READ(0xc0, { op_cmp(c, c->a, v); })                         /* CMP */
    ALU_READ(0xe0, { op_sbc(c, v); })                               /* SBC */

    WRITE_OP(0x85, am_zp(c), c->a)                                  /* STA */
    WRITE_OP(0x95, am_zpi(c, c->x), c->a)
    WRITE_OP(0x8d, am_abs(c), c->a)
    WRITE_OP(0x9d, am_absi_w(c, c->x), c->a)
    WRITE_OP(0x99, am_absi_w(c, c->y), c->a)
    WRITE_OP(0x81, am_indx(c), c->a)
    WRITE_OP(0x91, am_indy_w(c), c->a)
    WRITE_OP(0x86, am_zp(c), c->x)                                  /* STX */
    WRITE_OP(0x96, am_zpi(c, c->y), c->x)
    WRITE_OP(0x8e, am_abs(c), c->x)
    WRITE_OP(0x84, am_zp(c), c->y)                                  /* STY */
    WRITE_OP(0x94, am_zpi(c, c->x), c->y)
    WRITE_OP(0x8c, am_abs(c), c->y)

    IMM_OP (0xa2, { c->x = v; set_nz(c, v); })                      /* LDX */
    READ_OP(0xa6, am_zp(c), { c->x = v; set_nz(c, v); })
    READ_OP(0xb6, am_zpi(c, c->y), { c->x = v; set_nz(c, v); })
    READ_OP(0xae, am_abs(c), { c->x = v; set_nz(c, v); })
    READ_OP(0xbe, am_absi_r(c, c->y), { c->x = v; set_nz(c, v); })
    IMM_OP (0xa0, { c->y = v; set_nz(c, v); })                      /* LDY */
    READ_OP(0xa4, am_zp(c), { c->y = v; set_nz(c, v); })
    READ_OP(0xb4, am_zpi(c, c->x), { c->y = v; set_nz(c, v); })
    READ_OP(0xac, am_abs(c), { c->y = v; set_nz(c, v); })
    READ_OP(0xbc, am_absi_r(c, c->x), { c->y = v; set_nz(c, v); })

    IMM_OP (0xe0, { op_cmp(c, c->x, v); })                          /* CPX */
    READ_OP(0xe4, am_zp(c), { op_cmp(c, c->x, v); })
    READ_OP(0xec, am_abs(c), { op_cmp(c, c->x, v); })
    IMM_OP (0xc0, { op_cmp(c, c->y, v); })                          /* CPY */
    READ_OP(0xc4, am_zp(c), { op_cmp(c, c->y, v); })
    READ_OP(0xcc, am_abs(c), { op_cmp(c, c->y, v); })
    READ_OP(0x24, am_zp(c), { op_bit(c, v); })                      /* BIT */
    READ_OP(0x2c, am_abs(c), { op_bit(c, v); })

    /* --- shifts, INC, DEC --- */
    RMW_GROUP(0x00, { v = op_asl(c, v); })                          /* ASL */
    RMW_GROUP(0x20, { v = op_rol(c, v); })                          /* ROL */
    RMW_GROUP(0x40, { v = op_lsr(c, v); })                          /* LSR */
    RMW_GROUP(0x60, { v = op_ror(c, v); })                          /* ROR */
    RMW_GROUP(0xc0, { v--; set_nz(c, v); })                         /* DEC */
    RMW_GROUP(0xe0, { v++; set_nz(c, v); })                         /* INC */
    IMPL_OP(0x0a, { c->a = op_asl(c, c->a); })
    IMPL_OP(0x2a, { c->a = op_rol(c, c->a); })
    IMPL_OP(0x4a, { c->a = op_lsr(c, c->a); })
    IMPL_OP(0x6a, { c->a = op_ror(c, c->a); })

    /* --- register transfers and implied --- */
    IMPL_OP(0xaa, { c->x = c->a; set_nz(c, c->x); })                /* TAX */
    IMPL_OP(0x8a, { c->a = c->x; set_nz(c, c->a); })                /* TXA */
    IMPL_OP(0xa8, { c->y = c->a; set_nz(c, c->y); })                /* TAY */
    IMPL_OP(0x98, { c->a = c->y; set_nz(c, c->a); })                /* TYA */
    IMPL_OP(0xba, { c->x = c->s; set_nz(c, c->x); })                /* TSX */
    IMPL_OP(0x9a, { c->s = c->x; })                                 /* TXS */
    IMPL_OP(0xe8, { c->x++; set_nz(c, c->x); })                     /* INX */
    IMPL_OP(0xca, { c->x--; set_nz(c, c->x); })                     /* DEX */
    IMPL_OP(0xc8, { c->y++; set_nz(c, c->y); })                     /* INY */
    IMPL_OP(0x88, { c->y--; set_nz(c, c->y); })                     /* DEY */
    IMPL_OP(0x18, { c->p &= (uint8_t)~P_C; })                       /* CLC */
    IMPL_OP(0x38, { c->p |= P_C; })                                 /* SEC */
    IMPL_OP(0x58, { c->p &= (uint8_t)~P_I; })                       /* CLI */
    IMPL_OP(0x78, { c->p |= P_I; })                                 /* SEI */
    IMPL_OP(0xb8, { c->p &= (uint8_t)~P_V; })                       /* CLV */
    IMPL_OP(0xd8, { c->p &= (uint8_t)~P_D; })                       /* CLD */
    IMPL_OP(0xf8, { c->p |= P_D; })                                 /* SED */
    IMPL_OP(0xea, { })                                              /* NOP */

    /* --- stack --- */
    case 0x48: rd(c, c->pc); push(c, c->a); break;                  /* PHA */
    case 0x08: rd(c, c->pc); push(c, c->p | P_B | P_U); break;      /* PHP */
    case 0x68: rd(c, c->pc); rd(c, 0x100 | c->s);                   /* PLA */
               c->a = pull(c); set_nz(c, c->a); break;
    case 0x28: rd(c, c->pc); rd(c, 0x100 | c->s);                   /* PLP */
               c->p = (uint8_t)((pull(c) & ~P_B) | P_U); break;

    /* --- jumps and calls --- */
    case 0x4c: c->pc = am_abs(c); break;                            /* JMP abs */
    case 0x6c: {                                                    /* JMP (ind) */
        uint16_t lo = fetch(c), hi = fetch(c);
        uint16_t t = rd(c, (uint16_t)(hi << 8 | lo));
        /* page wrap bug: the high byte comes from the same page */
        c->pc = (uint16_t)(t | rd(c, (uint16_t)(hi << 8 | (uint8_t)(lo + 1))) << 8);
        break;
    }
    case 0x20: {                                                    /* JSR */
        uint16_t lo = fetch(c);
        rd(c, 0x100 | c->s);
        push(c, (uint8_t)(c->pc >> 8));
        push(c, (uint8_t)c->pc);
        c->pc = (uint16_t)(lo | rd(c, c->pc) << 8);
        break;
    }
    case 0x60: {                                                    /* RTS */
        rd(c, c->pc);
        rd(c, 0x100 | c->s);
        uint16_t lo = pull(c);
        c->pc = (uint16_t)(lo | pull(c) << 8);
        rd(c, c->pc++);
        break;
    }
    case 0x40: {                                                    /* RTI */
        rd(c, c->pc);
        rd(c, 0x100 | c->s);
        c->p = (uint8_t)((pull(c) & ~P_B) | P_U);
        uint16_t lo = pull(c);
        c->pc = (uint16_t)(lo | pull(c) << 8);
        break;
    }
    case 0x00: {                                                    /* BRK */
        fetch(c);                       /* padding byte */
        push(c, (uint8_t)(c->pc >> 8));
        push(c, (uint8_t)c->pc);
        push(c, c->p | P_B | P_U);
        c->p |= P_I;
        uint16_t lo = rd(c, 0xfffe);
        c->pc = (uint16_t)(lo | rd(c, 0xffff) << 8);
        break;
    }

    /* --- branches --- */
    case 0x10: branch(c, !(c->p & P_N)); break;                     /* BPL */
    case 0x30: branch(c, c->p & P_N); break;                        /* BMI */
    case 0x50: branch(c, !(c->p & P_V)); break;                     /* BVC */
    case 0x70: branch(c, c->p & P_V); break;                        /* BVS */
    case 0x90: branch(c, !(c->p & P_C)); break;                     /* BCC */
    case 0xb0: branch(c, c->p & P_C); break;                        /* BCS */
    case 0xd0: branch(c, !(c->p & P_Z)); break;                     /* BNE */
    case 0xf0: branch(c, c->p & P_Z); break;                        /* BEQ */

    /* ===== undocumented opcodes ===== */
    RMW_ILLEGAL(0x00, { v = op_asl(c, v); c->a |= v; set_nz(c, c->a); })   /* SLO */
    RMW_ILLEGAL(0x20, { v = op_rol(c, v); c->a &= v; set_nz(c, c->a); })   /* RLA */
    RMW_ILLEGAL(0x40, { v = op_lsr(c, v); c->a ^= v; set_nz(c, c->a); })   /* SRE */
    RMW_ILLEGAL(0x60, { v = op_ror(c, v); op_adc(c, v); })                 /* RRA */
    RMW_ILLEGAL(0xc0, { v--; op_cmp(c, c->a, v); })                        /* DCP */
    RMW_ILLEGAL(0xe0, { v++; op_sbc(c, v); })                              /* ISC */

    WRITE_OP(0x87, am_zp(c), c->a & c->x)                           /* SAX */
    WRITE_OP(0x97, am_zpi(c, c->y), c->a & c->x)
    WRITE_OP(0x8f, am_abs(c), c->a & c->x)
    WRITE_OP(0x83, am_indx(c), c->a & c->x)

    READ_OP(0xa7, am_zp(c), { c->a = c->x = v; set_nz(c, v); })     /* LAX */
    READ_OP(0xb7, am_zpi(c, c->y), { c->a = c->x = v; set_nz(c, v); })
    READ_OP(0xaf, am_abs(c), { c->a = c->x = v; set_nz(c, v); })
    READ_OP(0xbf, am_absi_r(c, c->y), { c->a = c->x = v; set_nz(c, v); })
    READ_OP(0xa3, am_indx(c), { c->a = c->x = v; set_nz(c, v); })
    READ_OP(0xb3, am_indy_r(c), { c->a = c->x = v; set_nz(c, v); })
    IMM_OP (0xab, { c->a = c->x = (uint8_t)((c->a | CPU6502_ANE_MAGIC) & v);  /* LXA */
                    set_nz(c, c->a); })
    IMM_OP (0x8b, { c->a = (uint8_t)((c->a | CPU6502_ANE_MAGIC) & c->x & v); /* ANE */
                    set_nz(c, c->a); })
    READ_OP(0xbb, am_absi_r(c, c->y), {                             /* LAS */
        c->a = c->x = c->s = v & c->s; set_nz(c, c->a); })

    IMM_OP(0x0b, { c->a &= v; set_nz(c, c->a); set_flag(c, P_C, c->a & 0x80); }) /* ANC */
    IMM_OP(0x2b, { c->a &= v; set_nz(c, c->a); set_flag(c, P_C, c->a & 0x80); })
    IMM_OP(0x4b, { c->a = op_lsr(c, c->a & v); })                   /* ALR */
    IMM_OP(0x6b, { op_arr(c, v); })                                 /* ARR */
    IMM_OP(0xcb, {                                                  /* SBX */
        uint8_t ax = c->a & c->x;
        set_flag(c, P_C, ax >= v);
        c->x = (uint8_t)(ax - v); set_nz(c, c->x); })
    IMM_OP(0xeb, { op_sbc(c, v); })                                 /* SBC */

    case 0x9f: { uint16_t lo = fetch(c), hi = fetch(c);             /* SHA abs,Y */
                 store_and_high(c, lo, hi, c->y, c->a & c->x); break; }
    case 0x93: { uint8_t p = fetch(c);                              /* SHA (zp),Y */
                 uint16_t lo = rd(c, p), hi = rd(c, (uint8_t)(p + 1));
                 store_and_high(c, lo, hi, c->y, c->a & c->x); break; }
    case 0x9e: { uint16_t lo = fetch(c), hi = fetch(c);             /* SHX abs,Y */
                 store_and_high(c, lo, hi, c->y, c->x); break; }
    case 0x9c: { uint16_t lo = fetch(c), hi = fetch(c);             /* SHY abs,X */
                 store_and_high(c, lo, hi, c->x, c->y); break; }
    case 0x9b: { uint16_t lo = fetch(c), hi = fetch(c);             /* TAS abs,Y */
                 c->s = c->a & c->x;
                 store_and_high(c, lo, hi, c->y, c->s); break; }

    /* NOPs with operands (they still do their reads) */
    IMPL_OP(0x1a, { }) IMPL_OP(0x3a, { }) IMPL_OP(0x5a, { })
    IMPL_OP(0x7a, { }) IMPL_OP(0xda, { }) IMPL_OP(0xfa, { })
    IMM_OP(0x80, { (void)v; }) IMM_OP(0x82, { (void)v; }) IMM_OP(0x89, { (void)v; })
    IMM_OP(0xc2, { (void)v; }) IMM_OP(0xe2, { (void)v; })
    READ_OP(0x04, am_zp(c), { (void)v; }) READ_OP(0x44, am_zp(c), { (void)v; })
    READ_OP(0x64, am_zp(c), { (void)v; })
    READ_OP(0x14, am_zpi(c, c->x), { (void)v; }) READ_OP(0x34, am_zpi(c, c->x), { (void)v; })
    READ_OP(0x54, am_zpi(c, c->x), { (void)v; }) READ_OP(0x74, am_zpi(c, c->x), { (void)v; })
    READ_OP(0xd4, am_zpi(c, c->x), { (void)v; }) READ_OP(0xf4, am_zpi(c, c->x), { (void)v; })
    READ_OP(0x0c, am_abs(c), { (void)v; })
    READ_OP(0x1c, am_absi_r(c, c->x), { (void)v; }) READ_OP(0x3c, am_absi_r(c, c->x), { (void)v; })
    READ_OP(0x5c, am_absi_r(c, c->x), { (void)v; }) READ_OP(0x7c, am_absi_r(c, c->x), { (void)v; })
    READ_OP(0xdc, am_absi_r(c, c->x), { (void)v; }) READ_OP(0xfc, am_absi_r(c, c->x), { (void)v; })

    /* JAM: the CPU stops until reset */
    default:
        c->jammed = 1;
        c->pc--;
        break;
    }

    /* Interrupt poll: the state of the cycle before the last one */
    c->irq_take = c->irq_prev;
}

#undef READ_OP
#undef IMM_OP
#undef WRITE_OP
#undef RMW_OP
#undef IMPL_OP
#undef ALU_READ
#undef RMW_GROUP
#undef RMW_ILLEGAL
