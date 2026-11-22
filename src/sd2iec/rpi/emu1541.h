// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// emu1541.h - 1541 emulation on core 1, for rpi/browser.c (see emu1541.c)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef EMU1541_H
#define EMU1541_H

#include "dirent.h"

/*
 * Run the D64 'dent' (in directory 'path') in the emulated 1541 until
 * emu1541_request_stop(). Returns 0 when it ran and was stopped, or -1
 * with a message in note (max. 21 characters) if it could not start.
 * 'started' is called just before the emulation takes over core 1.
 */
int emu1541_run_d64(path_t *path, cbmdirent_t *dent, char *note, void (*started)(void));

/*
 * Use the file 'dent' (8 or 16 KB) as the drive ROM from now on, and keep
 * the choice in PISD2IEC.CFG. 0 = done, 1 = in use but not saved, -1 = not
 * a ROM (message in note).
 */
int emu1541_set_rom(path_t *path, cbmdirent_t *dent, char *note);

/* rpi/browser.c: the directory entry of list index 'index' if it is a D64,
   else 0 (the list does not change while the emulation runs) */
cbmdirent_t *browse_d64_dent(unsigned index);

/* rpi/system.c (core 3): set when the IEC RESET line goes low */
extern volatile uint8_t rpi_reset_seen;

#endif
