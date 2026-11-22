// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// rpi.h - registers of the BCM2710A1 (Pi Zero 2 W)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef RPI_H
#define RPI_H

#include <stdint.h>

#define PERIPH_BASE     0x3F000000UL
#define LOCAL_BASE      0x40000000UL
#define RAM_END         0x20000000UL        /* 512 MB */

#define REG32(addr)     (*(volatile uint32_t *)(uintptr_t)(addr))

/* GPIO */
#define GPIO_BASE       (PERIPH_BASE + 0x200000)
#define GPFSEL(n)       REG32(GPIO_BASE + 0x00 + 4 * (n))
#define GPSET0          REG32(GPIO_BASE + 0x1C)
#define GPSET1          REG32(GPIO_BASE + 0x20)
#define GPCLR0          REG32(GPIO_BASE + 0x28)
#define GPCLR1          REG32(GPIO_BASE + 0x2C)
#define GPLEV0          REG32(GPIO_BASE + 0x34)
#define GPLEV1          REG32(GPIO_BASE + 0x38)
#define GPPUD           REG32(GPIO_BASE + 0x94)
#define GPPUDCLK0       REG32(GPIO_BASE + 0x98)
#define GPPUDCLK1       REG32(GPIO_BASE + 0x9C)

/* PL011 UART0 */
#define UART0_BASE      (PERIPH_BASE + 0x201000)
#define UART0_DR        REG32(UART0_BASE + 0x00)
#define UART0_FR        REG32(UART0_BASE + 0x18)
#define UART0_IBRD      REG32(UART0_BASE + 0x24)
#define UART0_FBRD      REG32(UART0_BASE + 0x28)
#define UART0_LCRH      REG32(UART0_BASE + 0x2C)
#define UART0_CR        REG32(UART0_BASE + 0x30)
#define UART0_IMSC      REG32(UART0_BASE + 0x38)
#define UART0_ICR       REG32(UART0_BASE + 0x44)

#define UART_FR_BUSY    (1u << 3)
#define UART_FR_RXFE    (1u << 4)
#define UART_FR_TXFF    (1u << 5)

/* VideoCore mailbox */
#define MBOX_BASE       (PERIPH_BASE + 0xB880)
#define MBOX_READ       REG32(MBOX_BASE + 0x00)
#define MBOX_STATUS     REG32(MBOX_BASE + 0x18)
#define MBOX_WRITE      REG32(MBOX_BASE + 0x20)
#define MBOX_FULL       0x80000000u
#define MBOX_EMPTY      0x40000000u

/* Barriers */
static inline void dmb(void) { __asm__ volatile("dmb sy" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }

static inline unsigned current_el(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (unsigned)((v >> 2) & 3);
}

static inline unsigned core_id(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return (unsigned)(v & 0xff);
}

#endif
