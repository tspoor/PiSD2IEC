// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (src/avr/arch-config.h and src/lpc17xx/arch-config.h),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// arch-config.h - sd2iec architecture layer for the Raspberry Pi Zero 2 W
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Counterpart of src/avr/arch-config.h and src/lpc17xx/arch-config.h in
// sd2iec, for the Pi1541 HAT option B (CBMretro vertical).
//
// No interrupts: sd2iec's 100 Hz tick (SYSTEM_TICK_HANDLER in timer.c)
// runs on core 3 (see rpi/system.c), which also handles the ATN
// acknowledge.

#ifndef ARCH_CONFIG_H
#define ARCH_CONFIG_H

#include <stdint.h>
#include "hal/rpi.h"
#include "hal/hat.h"

#define BV(x) (1 << (x))

/*
 * ----- sd2iec's debug UART (rpi/uart.c) -----
 * PiSD2IEC has its own uart_putc/uart_getc/uart_init too (hal/uart.c). These
 * macros give sd2iec's versions a name of their own; config.h (and so this
 * file) comes before uart.h in every sd2iec file.
 */
#define uart_init    sd2iec_uart_init
#define uart_getc    sd2iec_uart_getc
#define uart_putc    sd2iec_uart_putc
#define uart_puthex  sd2iec_uart_puthex
#define uart_trace   sd2iec_uart_trace
#define uart_flush   sd2iec_uart_flush
#define uart_puts_P  sd2iec_uart_puts_P
#define uart_putcrlf sd2iec_uart_putcrlf

/* ----- Storage: SD card through the EMMC controller (rpi/sdcard.c) ----- */
#define HAVE_SD

/* ----- IEC bus: split lines, as on the HAT option B ----- */
#define IEC_SEPARATE_OUT

/*
 * Inputs: 8 bits, as on AVR.
 *
 * sd2iec was written for AVR and sometimes stores the bus state in a
 * uint8_t (e.g. iec_putc() in iec.c). On the Pi the IEC inputs are on
 * GPIO 21-26, so those bits would be lost. That's why IEC_INPUT reads
 * GPLEV0 once (all lines at the same instant) and puts the four lines in
 * bits 0-3. That keeps sd2iec's 8-bit assumption valid everywhere,
 * including the fast loaders.
 */
typedef uint8_t iec_bus_t;
#define IEC_BIT_ATN     BV(0)
#define IEC_BIT_DATA    BV(1)
#define IEC_BIT_CLOCK   BV(2)
#define IEC_BIT_SRQ     BV(3)

static inline __attribute__((always_inline)) iec_bus_t rpi_iec_input(void)
{
    uint32_t lev = GPLEV0;
    return (iec_bus_t)((((lev >> HAT_IN_ATN)   & 1u) << 0) |
                       (((lev >> HAT_IN_DATA)  & 1u) << 1) |
                       (((lev >> HAT_IN_CLOCK) & 1u) << 2) |
                       (((lev >> HAT_IN_SRQ)   & 1u) << 3));
}
#define IEC_INPUT       rpi_iec_input()

#if HAT_IEC_IN_INVERTED
#  define IEC_INPUTS_INVERTED
#  define IEC_ATN   (!(IEC_INPUT & IEC_BIT_ATN))
#  define IEC_DATA  (!(IEC_INPUT & IEC_BIT_DATA))
#  define IEC_CLOCK (!(IEC_INPUT & IEC_BIT_CLOCK))
#  define IEC_SRQ   (!(IEC_INPUT & IEC_BIT_SRQ))
#endif

/* Outputs through the 7406: state 1 = release the line, 0 = pull it low */
static inline __attribute__((always_inline)) void iec_rpi_out(uint32_t mask, unsigned int state)
{
#if HAT_IEC_OUT_INVERTED
    if (state) GPCLR0 = mask; else GPSET0 = mask;
#else
    if (state) GPSET0 = mask; else GPCLR0 = mask;
#endif
}
static inline __attribute__((always_inline)) void set_atn(unsigned int s)   { iec_rpi_out(BV(HAT_OUT_ATN), s); }
static inline __attribute__((always_inline)) void set_clock(unsigned int s) { iec_rpi_out(BV(HAT_OUT_CLOCK), s); }
static inline __attribute__((always_inline)) void set_data(unsigned int s)  { iec_rpi_out(BV(HAT_OUT_DATA), s); }
static inline __attribute__((always_inline)) void set_srq(unsigned int s)   { iec_rpi_out(BV(HAT_OUT_SRQ), s); }

/*
 * "ATN interrupt": on AVR/LPC, IEC_ATN_HANDLER pulls DATA low as soon as
 * ATN goes low. Here core 3 does that by polling ATN (rpi/system.c);
 * set_atn_irq() only sets a flag.
 */
#define IEC_ATN_HANDLER void iec_atn_handler(void)
void iec_atn_handler(void);
extern volatile uint8_t rpi_atn_irq;
static inline __attribute__((always_inline)) void set_atn_irq(uint8_t state)
{
    rpi_atn_irq = state;
}

/* Interrupts for ATN/CLOCK: not needed, core 3 polls (see above) */
static inline void iec_interrupts_init(void) { }

/* Core 0 already sets up the pins (iec_hw_init), before the other cores start */
static inline void iec_pins_connect(void) { }
static inline void parallel_init(void) { }

/*
 * The "display interrupt": on AVR/LPC an external display asks for
 * attention through a pin, after which sd2iec calls display_service() on
 * its own core when the bus is idle. PiSD2IEC uses it for the SD card
 * browser on the OLED: active while core 2 has requests waiting
 * (rpi/browser.c).
 */
static inline void display_intrq_init(void) { }
unsigned int display_intrq_active(void);

/* ----- 100 Hz tick: a plain function, core 3 calls it ----- */
#define SYSTEM_TICK_HANDLER void system_tick_handler(void)
void system_tick_handler(void);

/* ----- Device number: no DIP switches on the HAT ----- */
static inline void device_hw_address_init(void) { }
static inline uint8_t device_hw_address(void) { return 8; }

/* ----- LED: the HAT LED as drive LED, single LED (like uIEC) ----- */
#define SINGLE_LED

static inline void leds_init(void)
{
    /* HAT LED is already an output (hat_init on core 0) */
}

/* While the 1541 emulation runs, the drive LED is the emulated drive's */
extern volatile uint8_t rpi_emu_active;

static inline __attribute__((always_inline)) void set_led(uint8_t state)
{
    if (rpi_emu_active)
        return;
    if (state)
        GPSET0 = 1u << HAT_LED;
    else
        GPCLR0 = 1u << HAT_LED;
}

static inline void toggle_led(void)
{
    set_led(!((GPLEV0 >> HAT_LED) & 1u));
}

/* ----- Buttons (NEXT/PREV for swapping images): not yet ----- */
typedef uint8_t rawbutton_t;
#define BUTTON_NEXT BV(0)
#define BUTTON_PREV BV(1)

static inline rawbutton_t buttons_read(void)
{
    return BUTTON_NEXT | BUTTON_PREV;   /* active low: nothing pressed */
}

static inline void buttons_init(void) { }

#endif
