// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// uart.h - PL011 UART on GPIO 14 (TX) / 15 (RX)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef UART_H
#define UART_H

#define UART_CLOCK_HZ 48000000u

void uart_init(unsigned baud);
void uart_putc(char c);
void uart_puts(const char *s);       /* converts \n to \r\n */
int  uart_getc(void);                /* -1 if nothing has arrived */

#endif
