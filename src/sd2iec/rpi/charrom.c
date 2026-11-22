// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// charrom.c - the C64 character ROM as the OLED font
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// The C64 character ROM (CHARGEN, 4 KB, as VICE names it) is Commodore's
// and is not part of PiSD2IEC. If the user puts it in the root of the SD
// card, core 1 reads it once, on sd2iec's first idle moment, and core 2
// then draws the OLED with it (8x8 characters). Without it the built-in
// 5x7 font is used.

#include <stdint.h>
#include "config.h"
#include "fatops.h"
#include "ff.h"
#include "parser.h"
#include "storage.h"

uint8_t rpi_chargen[4096];
volatile uint8_t rpi_chargen_state;     /* enum chargen_state */

/* Core 1, from display_service() */
void rpi_load_chargen(void)
{
    static const char *const names[] = { "CHARGEN", "CHARGEN.BIN", "CHARGEN.ROM" };

    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        FIL fil;
        UINT got = 0;
        uint8_t extra;

        partition[0].fatfs.curr_dir = 0;
        if (f_open(&partition[0].fatfs, &fil, (const UCHAR *)names[i],
                   FA_READ | FA_OPEN_EXISTING) != FR_OK)
            continue;
        FRESULT res = f_read(&fil, rpi_chargen, sizeof(rpi_chargen), &got);
        UINT more = 0;
        if (res == FR_OK && got == sizeof(rpi_chargen))
            res = f_read(&fil, &extra, 1, &more);   /* must be exactly 4 KB */
        f_close(&fil);
        if (res == FR_OK && got == sizeof(rpi_chargen) && more == 0) {
            __atomic_store_n(&rpi_chargen_state, CHARGEN_LOADED, __ATOMIC_RELEASE);
            return;
        }
    }
    __atomic_store_n(&rpi_chargen_state, CHARGEN_MISSING, __ATOMIC_RELEASE);
}
