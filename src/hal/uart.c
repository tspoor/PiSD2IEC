// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// uart.c - PL011 UART0
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "rpi.h"
#include "gpio.h"
#include "uart.h"

void uart_init(unsigned baud)
{
    UART0_CR = 0;
    while (UART0_FR & UART_FR_BUSY)
        ;

    /* GPIO 14/15 -> ALT0 (PL011). Disconnect GPIO 32/33 (Bluetooth). */
    gpio_fsel(32, GPIO_IN);
    gpio_fsel(33, GPIO_IN);
    gpio_fsel(14, GPIO_ALT0);
    gpio_fsel(15, GPIO_ALT0);
    gpio_pull(14, GPIO_PULL_NONE);
    gpio_pull(15, GPIO_PULL_UP);

    /* Divider in 1/64 steps: 4 * clk / baud */
    unsigned div = (4u * UART_CLOCK_HZ + baud / 2) / baud;
    UART0_ICR  = 0x7FF;
    UART0_IBRD = div >> 6;
    UART0_FBRD = div & 63;
    UART0_LCRH = (3u << 5) | (1u << 4);          /* 8N1, FIFO on */
    UART0_IMSC = 0;
    UART0_CR   = (1u << 0) | (1u << 8) | (1u << 9);  /* UART, TX, RX on */
}

void uart_putc(char c)
{
    while (UART0_FR & UART_FR_TXFF)
        ;
    UART0_DR = (unsigned char)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

int uart_getc(void)
{
    if (UART0_FR & UART_FR_RXFE)
        return -1;
    return (int)(UART0_DR & 0xFF);
}
