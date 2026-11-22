// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (the autoconf.h that sd2iec's Makefile generates),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// autoconf.h - sd2iec configuration for PiSD2IEC (Raspberry Pi Zero 2 W)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// In sd2iec the Makefile generates this file from configs/config-*; PiSD2IEC
// maintains it by hand. Options use sd2iec's names.

#ifndef AUTOCONF_H
#define AUTOCONF_H

#define CONFIG_ARCH_RPI             1
#define CONFIG_HARDWARE_VARIANT     200         /* PiSD2IEC's own number */
#define CONFIG_HARDWARE_NAME        PiSD2IEC

#define CONFIG_SD_AUTO_RETRIES      10
#define CONFIG_ERROR_BUFFER_SIZE    100
#define CONFIG_COMMAND_BUFFER_SIZE  250
#define CONFIG_BUFFER_COUNT         15
#define CONFIG_MAX_PARTITIONS       4

/* Version: in sd2iec the Makefile sets these (sd2iec 1.0.0 in development) */
#define VERSION                     "1.0.0"
#define LONGVERSION                 "-PiSD2IEC"

#define CONFIG_HAVE_IEC             1

/* sd2iec's debug output; goes to the console through a queue (rpi/uart.c) */
#define CONFIG_UART_DEBUG           1
#define CONFIG_COMMAND_CHANNEL_DUMP 1

/* sd2iec's display hooks; rpi/display.c passes them on to the OLED */
#define CONFIG_REMOTE_DISPLAY       1
#define CONFIG_M2I                  1

#endif
