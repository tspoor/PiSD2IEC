// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// uart.c - sd2iec's debug output (CONFIG_UART_DEBUG) on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// sd2iec runs on core 1 and must never wait for the slow UART. So
// everything goes into a lock-free byte queue (one writer: core 1, one
// reader: core 0). Core 0 reads it with sd2iec_log_getc() and puts it on
// the console. If the queue is full, characters are lost.

#include <stdint.h>
#include "config.h"
#include "uart.h"
#include "storage.h"

#define LOG_SIZE 8192u                  /* power of 2 */

static uint8_t  log_buf[LOG_SIZE];
static uint32_t log_head __attribute__((aligned(64)));  /* core 1 only */
static uint32_t log_tail __attribute__((aligned(64)));  /* core 0 only */
static uint32_t log_lost;

void uart_init(void) { }

void uart_putc(char c)
{
    uint32_t h = log_head;
    if (h - __atomic_load_n(&log_tail, __ATOMIC_ACQUIRE) >= LOG_SIZE) {
        log_lost++;
        return;
    }
    log_buf[h & (LOG_SIZE - 1)] = (uint8_t)c;
    __atomic_store_n(&log_head, h + 1, __ATOMIC_RELEASE);
}

unsigned char uart_getc(void)
{
    return 0;                           /* no input to sd2iec */
}

void uart_puthex(uint8_t num)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[num >> 4]);
    uart_putc(hex[num & 15]);
}

void uart_puts_P(const char *text)
{
    while (*text)
        uart_putc(*text++);
}

void uart_putcrlf(void)
{
    uart_putc('\r');
    uart_putc('\n');
}

void uart_trace(void *ptr, uint16_t start, uint16_t len)
{
    uint8_t *p = ptr;
    for (uint16_t i = 0; i < len; i++) {
        if ((i & 15) == 0) {
            uart_putcrlf();
            uart_puthex((uint8_t)((start + i) >> 8));
            uart_puthex((uint8_t)(start + i));
            uart_putc(':');
        }
        uart_putc(' ');
        uart_puthex(p[start + i]);
    }
    uart_putcrlf();
}

void uart_flush(void) { }

int sd2iec_log_getc(void)
{
    uint32_t t = log_tail;
    if (t == __atomic_load_n(&log_head, __ATOMIC_ACQUIRE))
        return -1;
    int c = log_buf[t & (LOG_SIZE - 1)];
    __atomic_store_n(&log_tail, t + 1, __ATOMIC_RELEASE);
    return c;
}
