// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (src/lpc17xx/llfl-common.c),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// llfl-common.c - timing layer for sd2iec's low-level fast loaders on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// sd2iec's fast loaders for lpc17xx (lpc17xx/llfl-*.c) are written against
// the API in lpc17xx/llfl-common.h: "wait until a line has level X and note
// the time", "set a line at reference time + T", "read the bus at reference
// time + T", with times in units of 100 ns. On the LPC1768 this uses timer
// capture and match hardware. Here it is done in software on core 1:
//
//  - time is the ARM generic counter (CNTPCT_EL0, 19.2 MHz, 52 ns per tick),
//    converted to 100 ns units and counted from llfl_setup(), just as the
//    LPC timers are reset there;
//  - a "capture" is a busy loop on GPLEV0 that notes the time as soon as the
//    line has the requested level (latency well below 100 ns);
//  - a "match" with NO_WAIT is remembered and carried out by the next busy
//    loop once its time has come; with WAIT it is carried out right away.
//
// Core 1 runs without interrupts, so nothing disturbs these loops.

#include <stdint.h>
#include "config.h"
#include "iec-bus.h"
#include "llfl-common.h"
#include "hal/timer.h"

/* CNTPCT runs at 19.2 MHz on the Pi Zero 2 W: 100 ns = 1.92 ticks = 48/25 */
#define TICKS_TO_100NS(t)   ((uint32_t)(((t) * 25) / 48))

uint32_t llfl_reference_time;

/* Number of time slots that were already in the past when requested */
volatile uint32_t llfl_late_count;

static uint64_t llfl_base;

/* Line changes scheduled with NO_WAIT */
enum { PEND_CLOCK, PEND_DATA, PEND_SRQ, PEND_COUNT };

static struct {
    uint32_t time;
    uint8_t  active;
    uint8_t  state;
} pending[PEND_COUNT];

uint32_t llfl_now(void)
{
    return TICKS_TO_100NS(timer_ticks() - llfl_base);
}

static void set_line(unsigned int line, unsigned int state)
{
    switch (line) {
    case PEND_CLOCK: set_clock(state); break;
    case PEND_DATA:  set_data(state);  break;
    default:         set_srq(state);   break;
    }
}

/* Carry out scheduled line changes whose time has come */
static inline void service_pending(uint32_t now)
{
    for (unsigned int i = 0; i < PEND_COUNT; i++) {
        if (pending[i].active && now >= pending[i].time) {
            set_line(i, pending[i].state);
            pending[i].active = 0;
        }
    }
}

/* Busy-wait until "time", carrying out scheduled changes meanwhile */
static void wait_until(uint32_t time)
{
    uint32_t now;

    while ((now = llfl_now()) < time)
        service_pending(now);
    service_pending(now);
}

/* ---------- utility functions ---------- */

void llfl_setup(void)
{
    for (unsigned int i = 0; i < PEND_COUNT; i++)
        pending[i].active = 0;
    llfl_base = timer_ticks();
}

void llfl_teardown(void)
{
    /* As on lpc17xx, unfinished match actions are dropped */
    for (unsigned int i = 0; i < PEND_COUNT; i++)
        pending[i].active = 0;
}

/**
 * llfl_wait_atn - wait until ATN has the specified level and capture time
 * @state: line level to wait for (0 low, 1 high)
 */
void llfl_wait_atn(unsigned int state)
{
    uint32_t now;

    do {
        now = llfl_now();
        service_pending(now);
    } while (!IEC_ATN != !state);

    llfl_reference_time = now;
}

/* llfl_wait_clock - see llfl_wait_atn, aborts on ATN low if atnabort is true */
void llfl_wait_clock(unsigned int state, llfl_atnabort_t atnabort)
{
    uint32_t now;

    do {
        now = llfl_now();
        service_pending(now);
        if (atnabort && !IEC_ATN)
            break;
    } while (!IEC_CLOCK != !state);

    llfl_reference_time = now;
}

/* llfl_wait_data - see llfl_wait_atn */
void llfl_wait_data(unsigned int state, llfl_atnabort_t atnabort)
{
    uint32_t now;

    do {
        now = llfl_now();
        service_pending(now);
        if (atnabort && !IEC_ATN)
            break;
    } while (!IEC_DATA != !state);

    llfl_reference_time = now;
}

static void set_line_at(unsigned int line, uint32_t time, unsigned int state,
                        llfl_wait_t wait)
{
    uint32_t target = llfl_reference_time + time;

    if (llfl_now() > target)
        llfl_late_count++;

    pending[line].time   = target;
    pending[line].state  = !!state;
    pending[line].active = 1;

    if (wait)
        wait_until(target);
}

/**
 * llfl_set_clock_at - sets clock line at a specified time offset
 * @time : change time in 100ns after llfl_reference_time
 * @state: new line state (0 low, 1 high)
 * @wait : wait until change happened if 1
 */
void llfl_set_clock_at(uint32_t time, unsigned int state, llfl_wait_t wait)
{
    set_line_at(PEND_CLOCK, time, state, wait);
}

/* llfl_set_data_at - see llfl_set_clock_at */
void llfl_set_data_at(uint32_t time, unsigned int state, llfl_wait_t wait)
{
    set_line_at(PEND_DATA, time, state, wait);
}

/* llfl_set_srq_at - see llfl_set_clock_at (nice for debugging) */
void llfl_set_srq_at(uint32_t time, unsigned int state, llfl_wait_t wait)
{
    set_line_at(PEND_SRQ, time, state, wait);
}

/**
 * llfl_read_bus_at - reads the IEC bus at a certain time
 * @time: read time in 100ns after llfl_reference_time
 */
uint32_t llfl_read_bus_at(uint32_t time)
{
    uint32_t target = llfl_reference_time + time;

    if (llfl_now() >= target)
        llfl_late_count++;

    wait_until(target);
    return iec_bus_read();
}

/* ---------- generic 2-bit transfers (as in sd2iec's lpc17xx code) ---------- */

/**
 * llfl_generic_load_2bit - generic 2-bit fastloader transmit
 * @def : pointer to fastloader definition struct
 * @byte: data byte
 */
void llfl_generic_load_2bit(const generic_2bit_t *def, uint8_t byte)
{
    unsigned int i;

    byte ^= def->eorvalue;

    for (i = 0; i < 4; i++) {
        llfl_set_clock_at(def->pairtimes[i], byte & (1 << def->clockbits[i]), NO_WAIT);
        llfl_set_data_at (def->pairtimes[i], byte & (1 << def->databits[i]),  WAIT);
    }
}

/**
 * llfl_generic_save_2bit - generic 2-bit fastsaver receive
 * @def: pointer to fastloader definition struct
 */
uint8_t llfl_generic_save_2bit(const generic_2bit_t *def)
{
    unsigned int i;
    uint8_t result = 0;

    for (i = 0; i < 4; i++) {
        uint32_t bus = llfl_read_bus_at(def->pairtimes[i]);

        result |= (!!(bus & IEC_BIT_CLOCK)) << def->clockbits[i];
        result |= (!!(bus & IEC_BIT_DATA))  << def->databits[i];
    }

    return result ^ def->eorvalue;
}
