// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (src/main.c and src/lpc17xx/system.c),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// system.c - sd2iec on the Pi's cores
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Core 1: sd2iec itself (sd2iec_core_main), like sd2iec's main(): IEC bus,
//         DOS, FAT and SD card, without interrupts.
//
// Core 3: what interrupts do on AVR/LPC17xx, plus a bus trace:
//   - the 100 Hz tick: SYSTEM_TICK_HANDLER (timer.c): ticks, drive LED;
//   - the ATN interrupt: as soon as ATN goes low while sd2iec has it
//     enabled (set_atn_irq), pull DATA low (IEC_ATN_HANDLER, iec.c).
//     Core 3 polls ATN, so this happens within a few microseconds, even
//     while core 1 is busy with the SD card.

#include <stdint.h>
#include "hal/timer.h"
#include "config.h"
#include "buffers.h"
#include "bus.h"
#include "diskchange.h"
#include "diskio.h"
#include "display.h"
#include "eeprom-conf.h"
#include "filesystem.h"
#include "iec-bus.h"
#include "led.h"
#include "system.h"
#include "timer.h"
#include "uart.h"
#include "hal/power.h"
#include "storage.h"
#include "emu1541.h"

volatile uint8_t rpi_atn_irq;
volatile uint8_t rpi_reset_seen;
volatile uint8_t rpi_trace_in_emu;      /* 'x': keep tracing during 1541 mode */

/*
 * Bus trace (logic analyzer): core 3 records every change of the IEC
 * inputs and of our own IEC outputs, with a timestamp. A ring buffer that
 * overwrites old entries; core 0 reads it out.
 */
#define TRACE_SIZE 4096u                /* power of 2 */
/* Raw GPLEV0 bits (not sd2iec's 8-bit IEC_BIT_*): inputs and outputs */
#define TRACE_MASK (BV(HAT_IN_ATN) | BV(HAT_IN_CLOCK) | BV(HAT_IN_DATA) | \
                    BV(HAT_OUT_ATN) | BV(HAT_OUT_CLOCK) | BV(HAT_OUT_DATA))

static struct { uint64_t t; uint32_t v; } trace[TRACE_SIZE];
static uint32_t trace_head;

uint32_t bustrace_head(void)
{
    return __atomic_load_n(&trace_head, __ATOMIC_ACQUIRE);
}

void bustrace_entry(uint32_t i, uint64_t *t, uint32_t *v)
{
    *t = trace[i & (TRACE_SIZE - 1)].t;
    *v = trace[i & (TRACE_SIZE - 1)].v;
}
static volatile int sd2iec_ready;

/* ----- system.h ----- */

void system_init_early(void) { }
void system_init_late(void) { }
void system_sleep(void) { }             /* waiting = polling */
void disable_interrupts(void) { }
void enable_interrupts(void) { }

/* DOS commands UI/UJ: resetting the drive = cold rebooting the Pi */
void system_reset(void)
{
    pi_reboot();
}

/* ----- Core 3: tick and ATN ----- */

void sd2iec_tick_main(void)
{
    const uint64_t period = timer_us_to_ticks(10000);  /* 10 ms */
    uint64_t next = timer_ticks() + period;
    uint32_t atn_prev = GPLEV0 & BV(HAT_IN_ATN);
    uint32_t reset_prev = GPLEV0 & BV(HAT_IN_RESET);
    uint32_t trace_prev = ~0u;

    for (;;) {
        /*
         * During the 1541 emulation core 1 needs the peripheral bus every
         * microsecond; then this core only keeps sd2iec's tick (with the
         * ARM counter, which is local to the core) and leaves the GPIOs
         * alone, unless the trace was asked for ('x' on the console).
         */
        if (rpi_emu_active && !rpi_trace_in_emu) {
            if (timer_ticks() >= next) {
                system_tick_handler();
                next += period;
            }
            continue;
        }

        uint32_t lev = GPLEV0;          /* raw: includes the outputs, for the trace */

        /* ATN edge: high -> low (falling edge, like the interrupt) */
        uint32_t atn = lev & BV(HAT_IN_ATN);
        if (atn_prev && !atn && rpi_atn_irq)
            iec_atn_handler();
        atn_prev = atn;

        /* IEC RESET pulled low (C64 reset, or the HAT's reset button) */
        uint32_t reset = lev & BV(HAT_IN_RESET);
        if (reset_prev && !reset)
            rpi_reset_seen = 1;
        reset_prev = reset;

        /* Bus trace */
        uint32_t v = lev & TRACE_MASK;
        if (v != trace_prev) {
            uint32_t h = trace_head;
            trace[h & (TRACE_SIZE - 1)].t = timer_ticks();
            trace[h & (TRACE_SIZE - 1)].v = v;
            __atomic_store_n(&trace_head, h + 1, __ATOMIC_RELEASE);
            trace_prev = v;
        }

        if (timer_ticks() >= next) {
            system_tick_handler();
            next += period;
        }
    }
}

/* ----- Core 1: sd2iec ----- */

void sd2iec_core_main(void)
{
    /* Same order as sd2iec's main(), minus what core 0 already did
       (pins, clocks, UART) */
    leds_init();
    set_busy_led(1);
    set_dirty_led(0);

    bus_interface_init();
    buffers_init();
    buttons_init();
    bus_init();
    disk_init();
    read_configuration();
    filesystem_init(0);
    change_init();

    /* Like sd2iec's main() with CONFIG_REMOTE_DISPLAY: register the display */
    if (display_init(0, 0)) {
        display_address(device_address);
        display_current_part(0);
    }

    uart_puts_P(PSTR("\r\nsd2iec " VERSION " #"));
    uart_puthex(device_address);
    uart_putcrlf();

    set_busy_led(0);
    __atomic_store_n(&sd2iec_ready, 1, __ATOMIC_RELEASE);

    bus_mainloop();
}

int sd2iec_is_ready(void)
{
    return __atomic_load_n(&sd2iec_ready, __ATOMIC_ACQUIRE);
}

uint8_t sd2iec_device_address(void)
{
    return device_address;
}

int sd2iec_disk_ok(void)
{
    return disk_state == DISK_OK;
}
