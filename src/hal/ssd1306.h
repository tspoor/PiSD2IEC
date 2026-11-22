// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// ssd1306.h - OLED 128x64 (SSD1306) via I2C
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Drawing goes into a framebuffer in RAM; ssd1306_flush() sends the whole
// display in one I2C transaction (~23 ms at 400 kHz). Text: 8x8 cells,
// 16 characters x 8 lines. With ssd1306_set_font() the C64 character ROM
// draws them (and PETSCII graphics, bytes 0xA0..0xFE); otherwise, and for
// characters the C64 font lacks, the built-in 5x7 font.
//
// Use from one core only (the ui core).

#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>

#define SSD1306_WIDTH   128
#define SSD1306_PAGES   8               /* 8 lines of 8 pixels */
#define SSD1306_COLS    16              /* characters per line */

/* 1: rotate the display 180 degrees (if it is mounted upside down) */
#include "hat.h"
#ifndef SSD1306_FLIP
#define SSD1306_FLIP    HAT_OLED_FLIP
#endif

/* Looks for the display at 0x3C and 0x3D. Returns the address, or 0 if none. */
uint8_t ssd1306_init(void);

/* Draw text with this C64 character set (2 KB: 256 characters x 8 bytes,
   e.g. the lowercase half of CHARGEN); NULL = the built-in 5x7 font */
void ssd1306_set_font(const uint8_t *charset);

void ssd1306_clear(void);
/* Text from column col (characters), line row; invert: white box, black text */
void ssd1306_text(unsigned col, unsigned row, const char *s, int invert);
/* Whole line 'row' with text that starts px pixels to the left of the
   screen (for scrolling long names); the rest of the line is blank */
void ssd1306_text_scroll(unsigned row, const char *s, int invert, unsigned px);
/* Horizontal line at pixel row y (0..63) */
void ssd1306_hline(unsigned y);
/* Framebuffer to the display. Returns 1 on success. */
int  ssd1306_flush(void);

#endif
