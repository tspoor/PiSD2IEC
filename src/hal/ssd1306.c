// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// ssd1306.c - OLED 128x64 (SSD1306) via I2C
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include <stdint.h>
#include <string.h>
#include "i2c.h"
#include "font5x7.h"
#include "ssd1306.h"

#define CTRL_CMD    0x00                /* following bytes are commands */
#define CTRL_DATA   0x40                /* following bytes are image data */

static uint8_t fb[SSD1306_WIDTH * SSD1306_PAGES];
static uint8_t addr;
static const uint8_t *font;             /* C64 character set, or NULL */

static int cmds(const uint8_t *c, unsigned n)
{
    return i2c_write(addr, CTRL_CMD, c, n);
}

uint8_t ssd1306_init(void)
{
    static const uint8_t init[] = {
        0xAE,               /* display off */
        0xD5, 0x80,         /* clock divider */
        0xA8, 0x3F,         /* multiplex: 64 rows */
        0xD3, 0x00,         /* no offset */
        0x40,               /* start line 0 */
        0x8D, 0x14,         /* internal charge pump on */
        0x20, 0x00,         /* horizontal addressing */
#if SSD1306_FLIP
        0xA0, 0xC0,         /* rotated 180 degrees */
#else
        0xA1, 0xC8,         /* columns and rows mirrored: normal orientation */
#endif
        0xDA, 0x12,         /* COM pins for 128x64 */
        0x81, 0xCF,         /* contrast */
        0xD9, 0xF1,         /* precharge */
        0xDB, 0x40,         /* VCOMH */
        0xA4,               /* show RAM contents */
        0xA6,               /* not inverted */
        0xAF,               /* display on */
    };
    static const uint8_t try_addr[] = { 0x3C, 0x3D };

    for (unsigned i = 0; i < sizeof(try_addr); i++) {
        addr = try_addr[i];
        if (cmds(init, sizeof(init))) {
            ssd1306_clear();
            ssd1306_flush();
            return addr;
        }
    }
    addr = 0;
    return 0;
}

void ssd1306_clear(void)
{
    for (unsigned i = 0; i < sizeof(fb); i++)
        fb[i] = 0;
}

void ssd1306_set_font(const uint8_t *charset)
{
    font = charset;
}

/* C64 screen code for a character (lowercase set), or -1 if it has none */
static int screen_code(unsigned c)
{
    if (c >= 0x20 && c <= 0x3F)
        return (int)c;
    if (c == '@')
        return 0x00;
    if (c >= 'a' && c <= 'z')
        return (int)(c - 0x60);
    if (c >= 'A' && c <= 'Z')
        return (int)c;
    if (c == '[')
        return 0x1B;
    if (c == ']')
        return 0x1D;
    if (c == '_')
        return 0x64;                    /* PETSCII 0xA4: low line */
    if (c >= 0xA0 && c <= 0xBF)         /* PETSCII graphics */
        return (int)(c - 0x40);
    if (c >= 0xC0 && c <= 0xFE)
        return (int)(c - 0x80);
    return -1;
}

/* The 8 pixel columns of a character (bit 0 = top), inverted if asked */
static void glyph(unsigned c, int invert, uint8_t out[8])
{
    uint8_t inv = invert ? 0xFF : 0x00;
    int sc = font ? screen_code(c) : -1;

    if (sc >= 0) {
        /* C64 glyph: 8 rows of 8 pixels (bit 7 left) -> 8 columns */
        const uint8_t *g = &font[sc * 8];
        for (unsigned i = 0; i < 8; i++) {
            uint8_t colbits = 0;
            for (unsigned r = 0; r < 8; r++)
                colbits |= (uint8_t)(((g[r] >> (7 - i)) & 1u) << r);
            out[i] = colbits ^ inv;
        }
        return;
    }
    if (c < FONT5X7_FIRST || c > FONT5X7_LAST)
        c = '?';
    const uint8_t *g = font5x7[c - FONT5X7_FIRST];
    out[0] = inv;
    for (unsigned i = 0; i < 5; i++)
        out[1 + i] = g[i] ^ inv;
    out[6] = out[7] = inv;
}

void ssd1306_text(unsigned col, unsigned row, const char *s, int invert)
{
    if (row >= SSD1306_PAGES)
        return;
    uint8_t *p = &fb[row * SSD1306_WIDTH];
    for (unsigned x = col * 8; *s && x + 8 <= SSD1306_WIDTH; s++, x += 8)
        glyph((unsigned char)*s, invert, &p[x]);
}

void ssd1306_text_scroll(unsigned row, const char *s, int invert, unsigned px)
{
    if (row >= SSD1306_PAGES)
        return;
    uint8_t *p = &fb[row * SSD1306_WIDTH];
    uint8_t g[8];

    /* Character 'i' starts at pixel i * 8 - px; the line is filled to the
       right edge (with spaces past the end of the text) */
    unsigned i = px / 8, x = 0, skip = px % 8;
    size_t len = strlen(s);
    while (x < SSD1306_WIDTH) {
        glyph(i < len ? (unsigned char)s[i] : ' ', invert, g);
        for (unsigned k = skip; k < 8 && x < SSD1306_WIDTH; k++)
            p[x++] = g[k];
        skip = 0;
        i++;
    }
}

void ssd1306_hline(unsigned y)
{
    if (y >= SSD1306_PAGES * 8)
        return;
    uint8_t *p = &fb[(y / 8) * SSD1306_WIDTH];
    uint8_t bit = (uint8_t)(1u << (y % 8));
    for (unsigned x = 0; x < SSD1306_WIDTH; x++)
        p[x] |= bit;
}

int ssd1306_flush(void)
{
    static const uint8_t window[] = {
        0x21, 0x00, SSD1306_WIDTH - 1,  /* columns 0..127 */
        0x22, 0x00, SSD1306_PAGES - 1,  /* pages 0..7 */
    };
    if (!addr || !cmds(window, sizeof(window)))
        return 0;
    return i2c_write(addr, CTRL_DATA, fb, sizeof(fb));
}
