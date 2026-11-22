// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// cores.h - division of work over the four cores, and the messages between them
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
//   core 0  system: startup, console (with sd2iec's debug output), reboot ('r')
//   core 1  sd2iec: IEC bus, DOS, FAT and SD card, without interrupts
//   core 2  ui: LEDs, buttons, buzzer, OLED display (SSD1306), SD card browser
//   core 3  sd2iec tick and ATN: what interrupts do on AVR/LPC
//
// Only core 0 writes to the console. The other cores send messages through
// a queue (lib/ring.h); each queue has exactly one writer and one reader.
// sd2iec (core 1) never waits for another core.

#ifndef CORES_H
#define CORES_H

#include "ring.h"

#define PISD2IEC_VERSION "0.4"

#define CORE_SYS    0
#define CORE_IEC    1
#define CORE_UI     2
#define CORE_TICK   3

/* Message types */
enum {
    MSG_BUTTON = 1,     /* UI  -> sys: arg = button number (0..4) | pressed << 8 */
    MSG_BEEP,           /* sys -> UI:  arg = frequency in Hz (80 ms)            */
    MSG_OLED,           /* UI  -> sys: arg = I2C address, 0 = none; bits 8+: I2C error */
    MSG_REBOOT,         /* sys -> UI:  Pi is about to reboot, show it on the OLED */
    MSG_REBOOT_OK,      /* UI  -> sys: message is shown, reboot may proceed      */
};

/* Queues, named writer_to_reader */
extern struct ring q_ui_to_sys;
extern struct ring q_sys_to_ui;

void ui_core_main(void);                /* ui_core.c  */

#endif
