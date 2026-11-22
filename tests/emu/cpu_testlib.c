// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// cpu_testlib.c - the 6502 core on the PC, as a shared library for tests
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// 64 KB of plain RAM as the bus, with a log of every bus cycle, so that
// cpu_test.py can check the core against per-cycle test suites and run
// whole test programs.

#include <stdint.h>
#include <string.h>

static uint8_t mem[65536];
static int     irq_line;

#define LOG_MAX 64
static struct { uint16_t addr; uint8_t val; uint8_t write; } log_buf[LOG_MAX];
static int log_len;

static inline uint8_t cpu_bus_read(uint16_t a)
{
    uint8_t v = mem[a];
    if (log_len < LOG_MAX) {
        log_buf[log_len].addr = a; log_buf[log_len].val = v; log_buf[log_len].write = 0;
    }
    log_len++;
    return v;
}

static inline void cpu_bus_write(uint16_t a, uint8_t v)
{
    mem[a] = v;
    if (log_len < LOG_MAX) {
        log_buf[log_len].addr = a; log_buf[log_len].val = v; log_buf[log_len].write = 1;
    }
    log_len++;
}

static inline int cpu_irq_line(void) { return irq_line; }

#include "../../src/emu/cpu6502_impl.h"

static struct cpu6502 cpu;

/* ----- API for ctypes ----- */

void t_set_mem(uint16_t a, uint8_t v) { mem[a] = v; }
uint8_t t_get_mem(uint16_t a) { return mem[a]; }
void t_load(const uint8_t *data, uint32_t len, uint16_t at)
{
    memcpy(mem + at, data, len > 65536u - at ? 65536u - at : len);
}
void t_clear(void) { memset(mem, 0, sizeof(mem)); }

void t_set_regs(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t p)
{
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = pc; cpu.a = a; cpu.x = x; cpu.y = y; cpu.s = s; cpu.p = p;
}

void t_get_regs(uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *s, uint8_t *p)
{
    *pc = cpu.pc; *a = cpu.a; *x = cpu.x; *y = cpu.y; *s = cpu.s; *p = cpu.p;
}

int t_jammed(void) { return cpu.jammed; }
void t_set_irq(int on) { irq_line = on; }

/* One instruction; returns the number of bus cycles */
int t_step(void)
{
    log_len = 0;
    cpu6502_step(&cpu);
    return log_len;
}

void t_log(int i, uint16_t *addr, uint8_t *val, uint8_t *write)
{
    *addr = log_buf[i].addr; *val = log_buf[i].val; *write = log_buf[i].write;
}

/* Run until the PC stops changing (a "JMP *" or "Bxx *" trap) */
uint16_t t_run_until_trap(uint64_t max_cycles, uint64_t *cycles)
{
    uint16_t last = 0xffff;
    cpu.cycles = 0;
    while (cpu.cycles < max_cycles) {
        uint16_t pc = cpu.pc;
        log_len = 0;
        cpu6502_step(&cpu);
        if (cpu.pc == pc && pc == last)
            break;
        last = pc;
    }
    *cycles = cpu.cycles;
    return cpu.pc;
}
