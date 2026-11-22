// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// printf.c - minimal printf without heap or buffers
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include <stdint.h>
#include "printf.h"
#include "uart.h"

static void out(char c)
{
    if (c == '\n')
        uart_putc('\r');
    uart_putc(c);
}

static void out_num(uint64_t v, unsigned base, int upper, int width, char pad, int neg)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[24];
    int i = 0;

    do {
        buf[i++] = dig[v % base];
        v /= base;
    } while (v);

    if (neg) {
        if (pad == '0') {
            out('-');
            width--;
        } else {
            buf[i++] = '-';
        }
    }
    for (int n = i; n < width; n++)
        out(pad);
    while (i)
        out(buf[--i]);
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0, lng = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = lng ? va_arg(ap, long) : va_arg(ap, int);
            out_num(v < 0 ? (uint64_t)-v : (uint64_t)v, 10, 0, width, pad, v < 0);
            break;
        }
        case 'u':
            out_num(lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0, width, pad, 0);
            break;
        case 'x':
        case 'X':
            out_num(lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16,
                    *fmt == 'X', width, pad, 0);
            break;
        case 'p':
            out('0');
            out('x');
            out_num((uintptr_t)va_arg(ap, void *), 16, 0, 16, '0', 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = 0;
            while (s[len])
                len++;
            for (int n = len; n < width; n++)
                out(' ');
            while (*s)
                out(*s++);
            break;
        }
        case 'c':
            out((char)va_arg(ap, int));
            break;
        case '%':
            out('%');
            break;
        case '\0':
            return;
        default:
            out('%');
            out(*fmt);
            break;
        }
    }
}

/*
 * One kprintf at a time, so lines from different cores don't get mixed
 * up. Normally only core 0 prints; the lock is there for exceptions on
 * the other cores.
 */
static uint32_t print_lock;
static volatile int panic_mode;

void kprintf_panic(void)
{
    panic_mode = 1;                     /* from now on without the lock */
}

void kprintf(const char *fmt, ...)
{
    int locked = !panic_mode;
    if (locked)
        while (__atomic_exchange_n(&print_lock, 1, __ATOMIC_ACQUIRE))
            ;

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    if (locked)
        __atomic_store_n(&print_lock, 0, __ATOMIC_RELEASE);
}
