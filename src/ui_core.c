// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// ui_core.c - core 2: user interface (LEDs, buttons, buzzer and the OLED)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Everything that may be slow or waits by itself (beeping, I2C to the
// display) happens here, so it never holds up the IEC core or core 0.
// Buttons are debounced without waiting: a new state only counts once it
// has been stable for 20 ms.
//
// The OLED (I2C0, GPIO 0/1) is refreshed 10 times a second, which costs
// ~23 ms of I2C each time. Normally it shows what sd2iec is doing:
// directory, last file and error channel (sd2iec_get_status, filled
// through sd2iec's display hooks). It has 16 x 8 characters, drawn with
// the C64 character ROM if CHARGEN is on the SD card. Details (DOS
// commands, timing) are on the console.
//
// The buttons browse the SD card (rpi/browser.c does the work on core 1):
//   Up/Down  move the cursor (held down: repeats); from the status screen
//            they open the browser, as does Select
//   Select   directory or disk image: enter it (CD); a D64 instead runs
//            in the cycle-exact 1541 emulation. There Up/Down choose
//            another D64 of the same directory, Select puts it in the
//            drive (disk change) and Exit goes back to sd2iec;
//            file: make it the file that LOAD"*",8,1 loads
//   Exit     one level up / out of a disk image; at the root: back to the
//            status screen
//   Add      file: use it as the ROM of the emulated 1541 (8 or 16 KB,
//            e.g. JiffyDOS); kept in PISD2IEC.CFG
// After 30 seconds without a button the status screen returns.

#include <stdint.h>
#include <string.h>
#include "timer.h"
#include "hat.h"
#include "rpi.h"
#include "sd2iec/rpi/storage.h"
#include "ssd1306.h"
#include "i2c.h"
#include "cores.h"

#define BLINK_US     500000u
#define DEBOUNCE_US  20000u
#define BEEP_MS      80u
#define OLED_US      100000u
#define OLED_EMU_US  500000u            /* 1541 mode: fewer I2C transfers */

#define BROWSE_ROWS     6               /* list lines between title and footer */
#define BROWSE_IDLE_US  30000000u       /* back to the status screen */
#define REPEAT_DELAY_US 400000u         /* Up/Down held down */
#define REPEAT_US       80000u
#define MESSAGE_US      2500000u        /* "ready to load" / error screen */
#define SCROLL_PAUSE_US 1000000u        /* long names: still at start and end */
#define SCROLL_PX_S     40u             /* scroll speed, pixels per second */

enum ui_mode { UI_STATUS, UI_BROWSE, UI_MESSAGE, UI_EMU };

static enum ui_mode mode = UI_STATUS;
static unsigned cursor, top;
static int      waiting;                /* request sent, core 1 not done yet */
static uint32_t wait_gen;               /* info.gen when the request was sent */
static uint64_t last_input, message_until;
static enum ui_mode message_return;
static char     message_name[BROWSE_NAME_LEN + 1];
static int      message_ok;
static int      message_rom;            /* the message is about the 1541 ROM */
static unsigned emu_cur, emu_sel;       /* 1541 mode: disk in the drive / chosen next */

/* Next (dir = 1) or previous (dir = -1) D64 in the browser list, from 'from' */
static unsigned next_d64(unsigned from, int dir)
{
    struct browse_info bi;
    struct browse_entry e;
    browse_snapshot(&bi, 0, 0, 0);
    if (!bi.count)
        return from;
    unsigned i = from;
    for (unsigned n = 0; n < bi.count; n++) {
        i = (i + bi.count + (unsigned)dir) % bi.count;
        browse_snapshot(&bi, i, 1, &e);
        if (e.kind == BROWSE_IMAGE && e.type[0] == 'D' && e.type[1] == '6' && e.type[2] == '4')
            return i;
    }
    return from;
}

/* Text padded with spaces to the display width */
static void pad_line(char *line, const char *text)
{
    unsigned n = 0;
    while (text[n] && n < SSD1306_COLS) {
        line[n] = text[n];
        n++;
    }
    while (n < SSD1306_COLS)
        line[n++] = ' ';
    line[SSD1306_COLS] = 0;
}

/*
 * Long names scroll to the left: still for a second, then pixel by pixel
 * until the end is visible, still for a second, and back to the right.
 * 'since' is when the text appeared; step rounds the offset down (whole
 * characters where the display is refreshed only twice a second).
 */
struct scroll {
    char     text[BROWSE_NAME_LEN + 1];
    uint64_t since;
};

static unsigned scroll_px(struct scroll *sc, const char *s, uint64_t now, unsigned step)
{
    if (strncmp(sc->text, s, BROWSE_NAME_LEN)) {
        strncpy(sc->text, s, BROWSE_NAME_LEN);
        sc->since = now;
    }
    unsigned len = (unsigned)strlen(s);
    if (len <= SSD1306_COLS)
        return 0;
    uint64_t dist  = (len - SSD1306_COLS) * 8u;
    uint64_t run   = dist * 1000000u / SCROLL_PX_S;
    uint64_t cycle = 2u * (SCROLL_PAUSE_US + run);
    uint64_t t     = (now - sc->since) * 1000000u / timer_freq() % cycle;
    uint64_t px;
    if (t < SCROLL_PAUSE_US)                        /* still at the start */
        px = 0;
    else if ((t -= SCROLL_PAUSE_US) < run)          /* to the left */
        px = t * SCROLL_PX_S / 1000000u;
    else if ((t -= run) < SCROLL_PAUSE_US)          /* still at the end */
        px = dist;
    else                                            /* back to the right */
        px = dist - (t - SCROLL_PAUSE_US) * SCROLL_PX_S / 1000000u;
    return (unsigned)(px - px % step);
}

/* Inverted line: left text, right text at the right edge */
static void bar_line(unsigned row, const char *left, const char *right)
{
    char line[SSD1306_COLS + 1];
    unsigned rl = (unsigned)strlen(right);
    if (rl > SSD1306_COLS)
        rl = SSD1306_COLS;
    pad_line(line, "");
    for (unsigned i = 0; left[i] && i < SSD1306_COLS - rl; i++)
        line[i] = left[i];
    memcpy(&line[SSD1306_COLS - rl], right, rl);
    ssd1306_text(0, row, line, 1);
}

/* Device number as "#8" */
static void device_text(char *d, unsigned dev)
{
    *d++ = '#';
    if (dev >= 10)
        *d++ = (char)('0' + dev / 10);
    *d++ = (char)('0' + dev % 10);
    *d = 0;
}

static void oled_draw(void)
{
    struct sd2iec_status st;
    char dev[4], line[SSD1306_COLS + 2];    /* +1: pad_line(line + 1) */
    sd2iec_get_status(&st);

    ssd1306_clear();
    device_text(dev, st.address ? st.address : 8);
    bar_line(0, " PiSD2IEC " PISD2IEC_VERSION, dev);

    /* What sd2iec is doing (through sd2iec's own display hooks) */
    line[0] = '/';
    pad_line(line + 1, st.dir);
    line[SSD1306_COLS] = 0;
    ssd1306_text(0, 2, line, 0);
    if (st.file[0]) {
        ssd1306_text(0, 4, st.file_write ? "Save:" : "Load:", 0);
        ssd1306_text(0, 5, st.file, 0);
    }
    ssd1306_text(0, 7, st.msg, 0);

    ssd1306_flush();
}

/* ----- SD card browser ----- */

static void browse_send(enum browse_op op, uint32_t index, uint32_t gen)
{
    struct browse_info bi;
    browse_snapshot(&bi, 0, 0, 0);
    if (browse_request(op, index, gen)) {
        waiting  = 1;
        wait_gen = bi.gen;
    }
}

static void button_pressed(unsigned b, uint64_t now)
{
    struct browse_info bi;
    browse_snapshot(&bi, 0, 0, 0);
    last_input = now;

    if (mode == UI_EMU) {
        /* Up/Down choose another D64 from the same directory, Select puts it
           in the drive, Exit goes back to sd2iec */
        if (b == HAT_BTN_EXIT) {
            emu1541_request_stop();
        } else if (b == HAT_BTN_UP) {
            emu_sel = next_d64(emu_sel, -1);
        } else if (b == HAT_BTN_DOWN) {
            emu_sel = next_d64(emu_sel, 1);
        } else if (b == HAT_BTN_SELECT && emu_sel != emu_cur) {
            emu1541_request_swap(emu_sel);
            emu_cur = emu_sel;
        }
        return;
    }
    if (mode != UI_BROWSE) {
        if (b == HAT_BTN_SELECT || b == HAT_BTN_UP || b == HAT_BTN_DOWN) {
            mode = UI_BROWSE;
            browse_send(BROWSE_LIST, 0, 0);
        }
        return;
    }
    if (waiting)
        return;                         /* core 1 is still busy */

    switch (b) {
    case HAT_BTN_UP:
        if (bi.count)
            cursor = cursor ? cursor - 1 : bi.count - 1;
        break;
    case HAT_BTN_DOWN:
        if (bi.count)
            cursor = cursor + 1 < bi.count ? cursor + 1 : 0;
        break;
    case HAT_BTN_SELECT:
        if (cursor < bi.count) {
            struct browse_entry e;
            browse_snapshot(&bi, cursor, 1, &e);
            memcpy(message_name, e.name, sizeof(message_name));
            browse_send(BROWSE_ENTER, cursor, bi.gen);
        }
        break;
    case HAT_BTN_EXIT:
        if (bi.at_root)
            mode = UI_STATUS;
        else
            browse_send(BROWSE_UP, 0, 0);
        break;
    case HAT_BTN_ADD:                   /* file: use it as the 1541 ROM */
        if (cursor < bi.count) {
            struct browse_entry e;
            browse_snapshot(&bi, cursor, 1, &e);
            if (e.kind == BROWSE_FILE) {
                memcpy(message_name, e.name, sizeof(message_name));
                browse_send(BROWSE_SET_ROM, cursor, bi.gen);
            }
        }
        break;
    default:
        break;
    }
}

/* Follow core 1: finished requests, and directory changes by the C64 */
static void browse_update(uint64_t now)
{
    struct browse_info bi;
    browse_snapshot(&bi, 0, 0, 0);

    if (waiting) {
        if (bi.gen == wait_gen)
            return;
        waiting = 0;
        if (bi.result == BROWSE_EMU) {
            mode = UI_EMU;              /* core 1 is the 1541 until Exit */
            waiting = 1;
            wait_gen = bi.gen;
            emu_cur = emu_sel = cursor; /* the D64 that was selected */
            return;
        }
        if (mode == UI_EMU) {           /* emulation stopped: new list */
            mode = UI_BROWSE;
            last_input = now;
        }
        if (bi.result == BROWSE_FILE_READY || bi.result == BROWSE_FAILED ||
            bi.result == BROWSE_ROM_SET) {
            message_ok     = bi.result != BROWSE_FAILED;
            message_rom    = bi.result == BROWSE_ROM_SET;
            message_return = bi.result == BROWSE_FILE_READY ? UI_STATUS : UI_BROWSE;
            message_until  = now + timer_us_to_ticks(MESSAGE_US);
            mode = UI_MESSAGE;
            if (bi.result != BROWSE_FAILED)
                return;                 /* list unchanged, keep the cursor */
        }
        cursor = bi.cursor;
        return;
    }

    if (mode == UI_BROWSE) {
        struct sd2iec_status st;
        sd2iec_get_status(&st);
        if (st.dir_count != bi.dir_count)   /* e.g. CD from the C64 */
            browse_send(BROWSE_LIST, 0, 0);
        else if (now - last_input > timer_us_to_ticks(BROWSE_IDLE_US))
            mode = UI_STATUS;
    } else if (mode == UI_MESSAGE && now >= message_until) {
        mode = message_return;
        last_input = now;
    }
}

static void oled_browse(uint64_t now)
{
    static struct scroll title_scroll, name_scroll;
    struct browse_info bi;
    struct browse_entry e[BROWSE_ROWS];
    char line[BROWSE_NAME_LEN + 2];         /* '/' + title */

    browse_snapshot(&bi, 0, 0, 0);
    if (bi.count == 0)
        cursor = 0;
    else if (cursor >= bi.count)
        cursor = bi.count - 1;
    if (cursor < top)
        top = cursor;
    if (cursor >= top + BROWSE_ROWS)
        top = cursor - BROWSE_ROWS + 1;
    if (top + BROWSE_ROWS > bi.count)
        top = bi.count > BROWSE_ROWS ? bi.count - BROWSE_ROWS : 0;
    browse_snapshot(&bi, top, BROWSE_ROWS, e);

    ssd1306_clear();

    /* Title: the directory */
    line[0] = '/';
    strncpy(line + 1, bi.title, BROWSE_NAME_LEN);
    line[BROWSE_NAME_LEN + 1] = 0;
    ssd1306_text_scroll(0, line, 1, scroll_px(&title_scroll, line, now, 1));

    if (waiting) {
        ssd1306_text(3, 3, "Reading...", 0);
    } else if (bi.count == 0) {
        ssd1306_text(4, 3, "(empty)", 0);
    } else {
        /* One name per line, the cursor line inverted */
        for (unsigned r = 0; r < BROWSE_ROWS && top + r < bi.count; r++) {
            if (top + r == cursor)      /* a long name scrolls */
                ssd1306_text_scroll(1 + r, e[r].name, 1,
                                    scroll_px(&name_scroll, e[r].name, now, 1));
            else
                ssd1306_text(0, 1 + r, e[r].name, 0);
        }

        /* Footer: type of the selected entry, position "12/345" */
        struct browse_entry sel = e[cursor - top];
        char pos[16];
        unsigned p = sizeof(pos) - 1;
        pos[p] = 0;
        for (unsigned v = bi.count; ; v /= 10) {
            pos[--p] = (char)('0' + v % 10);
            if (v < 10)
                break;
        }
        pos[--p] = '/';
        for (unsigned v = cursor + 1; ; v /= 10) {
            pos[--p] = (char)('0' + v % 10);
            if (v < 10)
                break;
        }
        unsigned plen = sizeof(pos) - 1 - p;
        pad_line(line, sel.type);
        memcpy(&line[SSD1306_COLS - plen], &pos[p], plen);
        ssd1306_text(0, 7, line, 0);
    }
    ssd1306_flush();
}

static void oled_message(uint64_t now)
{
    static struct scroll name_scroll;
    struct sd2iec_status st;
    sd2iec_get_status(&st);

    ssd1306_clear();
    bar_line(0, " PiSD2IEC " PISD2IEC_VERSION, "");
    if (message_ok && message_rom) {
        struct browse_info bi;
        browse_snapshot(&bi, 0, 0, 0);
        ssd1306_text(0, 2, "1541 ROM:", 0);
        ssd1306_text_scroll(3, message_name, 0, scroll_px(&name_scroll, message_name, now, 1));
        ssd1306_text(0, 5, bi.note[0] ? bi.note : "for D64 files", 0);
    } else if (message_ok) {
        ssd1306_text(0, 2, "Ready to load:", 0);
        ssd1306_text_scroll(3, message_name, 0, scroll_px(&name_scroll, message_name, now, 1));
        ssd1306_text(0, 5, "LOAD\"*\",8,1", 0);
    } else {
        struct browse_info bi;
        browse_snapshot(&bi, 0, 0, 0);
        ssd1306_text(0, 2, "Could not open:", 0);
        ssd1306_text_scroll(3, message_name, 0, scroll_px(&name_scroll, message_name, now, 1));
        ssd1306_text(0, 5, bi.note[0] ? bi.note : st.msg[0] ? st.msg : "-", 0);
    }
    ssd1306_flush();
}

/* Number to text */
static char *utoa_dec(char *p, unsigned v)
{
    char tmp[11];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void oled_emu(uint64_t now)
{
    static struct scroll disk_scroll, next_scroll;
    struct browse_info bi;
    struct browse_entry cur;
    struct emu1541_status es;
    char line[SSD1306_COLS + 1], *p;

    emu1541_get_status(&es);
    ssd1306_clear();
    bar_line(0, " 1541", "#8");
    ssd1306_text(0, 1, es.rom[0] ? es.rom : "-", 0);
    /* The disk under its long name from the browser list */
    browse_snapshot(&bi, emu_cur, 1, &cur);
    const char *disk = !es.active ? "starting..." : cur.name[0] ? cur.name : es.name;
    ssd1306_text_scroll(2, disk, 0, scroll_px(&disk_scroll, disk, now, 8));

    if (emu_sel != emu_cur) {
        /* Another disk chosen with Up/Down */
        struct browse_entry e;
        browse_snapshot(&bi, emu_sel, 1, &e);
        ssd1306_text(0, 4, "Next disk:", 0);
        ssd1306_text_scroll(5, e.name, 0, scroll_px(&next_scroll, e.name, now, 8));
        ssd1306_text(0, 7, "Select: insert", 0);
    } else {
        p = line;
        memcpy(p, "Track ", 6);
        p = utoa_dec(p + 6, es.track);
        if (es.halftrack)
            memcpy(p, ".5", 3);
        ssd1306_text(0, 4, line, 0);
        if (es.motor)
            ssd1306_text(0, 5, "Motor", 0);
        if (es.led)
            ssd1306_text(7, 5, "LED", 0);
        if (es.saving || es.swapping)
            ssd1306_text(0, 7, es.saving ? "Saving to SD..." : "Changing disk...", 0);
    }
    ssd1306_flush();
}

static void oled_rebooting(void)
{
    ssd1306_clear();
    bar_line(0, " PiSD2IEC " PISD2IEC_VERSION, "");
    ssd1306_text(2, 3, "Rebooting...", 0);
    ssd1306_flush();
}

void ui_core_main(void)
{
    const uint64_t blink_ticks    = timer_us_to_ticks(BLINK_US);
    const uint64_t debounce_ticks = timer_us_to_ticks(DEBOUNCE_US);

    uint64_t next_blink = timer_ticks();
    int led = 0;

    unsigned stable   = hat_buttons();  /* accepted state */
    unsigned raw_last = stable;
    uint64_t raw_since = timer_ticks();
    const uint64_t repeat_delay = timer_us_to_ticks(REPEAT_DELAY_US);
    const uint64_t repeat_ticks = timer_us_to_ticks(REPEAT_US);
    uint64_t next_repeat = 0;

    const uint64_t oled_ticks = timer_us_to_ticks(OLED_US);
    uint8_t oled = ssd1306_init();
    ring_put(&q_ui_to_sys, MSG_OLED, oled | (oled ? 0 : i2c_last_error() << 8), timer_ticks());
    uint64_t next_oled = timer_ticks();
    int font_set = 0;

    for (;;) {
        uint64_t now = timer_ticks();

        /* Heartbeat on the ACT LED (the HAT LED is sd2iec's drive LED) */
        if (now >= next_blink) {
            led = !led;
            pi_act_led(led);
            next_blink += blink_ticks;
        }

        /* Buttons */
        unsigned raw = hat_buttons();
        if (raw != raw_last) {
            raw_last  = raw;
            raw_since = now;
        } else if (raw != stable && now - raw_since >= debounce_ticks) {
            unsigned changed = raw ^ stable;
            stable = raw;
            for (unsigned i = 0; i < HAT_NUM_BUTTONS; i++) {
                if (!(changed & (1u << i)))
                    continue;
                unsigned pressed = (stable >> i) & 1u;
                ring_put(&q_ui_to_sys, MSG_BUTTON, i | (pressed << 8), now);
                if (pressed) {
                    button_pressed(i, now);
                    next_repeat = now + repeat_delay;
                }
            }
        }

        /* Up/Down held down: repeat */
        unsigned held = stable & ((1u << HAT_BTN_UP) | (1u << HAT_BTN_DOWN));
        if (held && raw == stable && now >= next_repeat && mode == UI_BROWSE) {
            button_pressed((held & (1u << HAT_BTN_UP)) ? HAT_BTN_UP : HAT_BTN_DOWN, now);
            next_repeat = now + repeat_ticks;
        }

        browse_update(now);

        /* Commands from core 0 */
        struct msg m;
        if (ring_get(&q_sys_to_ui, &m)) {
            if (m.type == MSG_BEEP) {
                hat_beep(m.arg, BEEP_MS);
            } else if (m.type == MSG_REBOOT) {
                if (oled)
                    oled_rebooting();
                ring_put(&q_ui_to_sys, MSG_REBOOT_OK, 0, timer_ticks());
                for (;;)                /* core 0 is now rebooting the Pi */
                    __asm__ volatile("wfe");
            }
        }

        /* Display; the C64 font as soon as core 1 has read CHARGEN */
        if (!font_set && __atomic_load_n(&rpi_chargen_state, __ATOMIC_ACQUIRE) == CHARGEN_LOADED) {
            ssd1306_set_font(rpi_chargen + 2048);   /* lowercase/uppercase set */
            font_set = 1;
        }
        if (oled && now >= next_oled) {
            if (mode == UI_BROWSE)
                oled_browse(now);
            else if (mode == UI_MESSAGE)
                oled_message(now);
            else if (mode == UI_EMU)
                oled_emu(now);
            else
                oled_draw();
            next_oled = now + (mode == UI_EMU ? timer_us_to_ticks(OLED_EMU_US) : oled_ticks);
        }
    }
}
