// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// display.c - sd2iec's display hooks to PiSD2IEC's OLED
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// With CONFIG_REMOTE_DISPLAY, sd2iec calls display_* at the right moments
// (file opened, DOS command, error channel, directory changed). On AVR/LPC
// that goes over I2C to an external display (sd2iec's display.c); here it
// goes into a shared status that core 2 shows on the OLED. No change to
// sd2iec itself.
//
// Writer: core 1 (sd2iec). Reader: core 2. A sequence number (seqlock)
// makes sure the reader never sees a half-updated status.

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "display.h"
#include "storage.h"
#include "petscii.h"

uint8_t display_found = 1;

static struct sd2iec_status st;
static uint32_t seq;

static void begin(void) { __atomic_store_n(&seq, seq + 1, __ATOMIC_RELEASE); __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void end(void)   { __atomic_thread_fence(__ATOMIC_SEQ_CST); __atomic_store_n(&seq, seq + 1, __ATOMIC_RELEASE); }

void sd2iec_get_status(struct sd2iec_status *out)
{
    for (;;) {
        uint32_t s1 = __atomic_load_n(&seq, __ATOMIC_ACQUIRE);
        if (s1 & 1)
            continue;                   /* core 1 is writing right now */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        memcpy(out, &st, sizeof(st));
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (__atomic_load_n(&seq, __ATOMIC_ACQUIRE) == s1)
            return;
    }
}

/*
 * PETSCII to ASCII. sd2iec converts file and directory names so they read
 * correctly in lowercase/uppercase mode ("Games"); messages and commands
 * are unshifted and belong in capitals ("00, OK"). PETSCII graphics stay
 * as they are (bytes 0xA1..0xFE); the OLED draws them with the C64
 * character ROM, or as '?' with the built-in font.
 */
static char pet2asc(uint8_t c, int names)
{
    if (c >= 0x41 && c <= 0x5A)
        return (char)(names ? c - 0x41 + 'a' : c);
    if (c >= 0xC1 && c <= 0xDA)
        return (char)(names ? c - 0xC1 + 'A' : c - 0xC1 + 'a');
    if (c >= 0x20 && c < 0x7F)
        return (char)c;
    if (c == 0xA4)                      /* sd2iec's PETSCII for '_' */
        return '_';
    if (c == 0xFF)                      /* sd2iec's PETSCII for '~' */
        return '~';
    if (c >= 0xA0)                      /* graphics: drawn with the C64 font */
        return (char)c;
    return '?';
}

void rpi_copy_pet(char *dst, unsigned max, const uint8_t *src, unsigned len, int names)
{
    unsigned n = 0;
    for (unsigned i = 0; i < len && n < max; i++) {
        if (src[i] == 0 || src[i] == 0x0D)
            break;
        if (src[i] == 0xA0)             /* padding in CBM names */
            continue;
        dst[n++] = pet2asc(src[i], names);
    }
    while (n && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

/* ----- sd2iec's display API (display.h) ----- */

uint8_t display_init(uint8_t len, uint8_t *message)
{
    (void)len; (void)message;
    return 1;
}

/* display_service() is the SD card browser: rpi/browser.c */

void display_send_cmd(uint8_t cmd, uint8_t len, const void *buf)
{
    begin();
    if (cmd == DISPLAY_DOSCOMMAND)
        rpi_copy_pet(st.cmd, SD2IEC_TEXT_LEN, buf, len, 0);
    else if (cmd == DISPLAY_ERRORCHANNEL)
        rpi_copy_pet(st.msg, SD2IEC_TEXT_LEN, buf, len, 0);
    end();
}

void display_send_cmd_byte(uint8_t cmd, uint8_t val)
{
    display_send_cmd(cmd, 1, &val);
}

/* Used by display_filename_read (a macro in display.h) */
void display_send_prefixed(uint8_t cmd, uint8_t prefixbyte, uint8_t len, const uint8_t *buffer)
{
    begin();
    if (cmd == DISPLAY_FILENAME_READ || cmd == DISPLAY_FILENAME_WRITE) {
        st.part = prefixbyte;
        st.file_write = (cmd == DISPLAY_FILENAME_WRITE);
        rpi_copy_pet(st.file, SD2IEC_NAME_LEN, buffer, len, 1);
        st.file_count++;
    }
    end();
}

void display_filename_write(uint8_t part, uint8_t len, const unsigned char *buf)
{
    display_send_prefixed(DISPLAY_FILENAME_WRITE, part, len, buf);
}

void display_address(uint8_t dev)
{
    begin();
    st.address = dev;
    end();
}

void display_current_part(uint8_t part)
{
    begin();
    st.part = part;
    end();
}

void display_current_directory(uint8_t part, const unsigned char *name)
{
    begin();
    st.part = part;
    rpi_copy_pet(st.dir, SD2IEC_NAME_LEN, name, 255, 1);
    st.dir_count++;
    end();
}

/* Menus (sd2iec's disk swap through the display) are not used by PiSD2IEC */
void display_menu_reset(void) { }
void display_menu_add(const unsigned char *string) { (void)string; }
void display_menu_show(uint8_t start) { (void)start; }
