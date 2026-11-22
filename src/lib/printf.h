// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// printf.h - small printf to the UART
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>

/* Supports %d %i %u %x %X %p %s %c %%, width, '0' padding, l/ll */
void kprintf(const char *fmt, ...) __attribute__((__format__(__printf__, 1, 2)));
void kvprintf(const char *fmt, va_list ap);   /* without the lock */

/* On an exception: ignore the lock (the halted core may be holding it). */
void kprintf_panic(void);

#endif
