// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// storage.h - sd2iec for the rest of PiSD2IEC
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Deliberately without sd2iec headers, so PiSD2IEC code (main.c) can use
// this without sd2iec's include path.

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* Base clock of the EMMC controller (mailbox, clock id 1); core 0, beforehand */
void sdcard_set_base_clock(uint32_t hz);

/* Card details after a successful initialisation */
void sdcard_describe(int *is_sdhc, uint32_t *nsectors, int *buswidth4);

/* Core 3: 100 Hz tick and ATN watch (what interrupts do on AVR/LPC) */
void sd2iec_tick_main(void);

/* Core 1: sd2iec itself (IEC bus, DOS, FAT, SD card). Does not return. */
void sd2iec_core_main(void);

/* After initialisation on core 1: 1 = ready */
int     sd2iec_is_ready(void);
uint8_t sd2iec_device_address(void);
int     sd2iec_disk_ok(void);

/*
 * Fast loaders (rpi/llfl-common.c): number of time slots that were already
 * past when requested. Should stay 0; if it grows, the timing is too tight.
 */
extern volatile uint32_t llfl_late_count;

/*
 * Bus trace from core 3: every change of the IEC inputs (ATN, CLK, DATA)
 * and of our own outputs, with timer_ticks(). bustrace_head() keeps
 * counting; the last 4096 entries can be read with bustrace_entry().
 * v holds the raw GPLEV0 bits (HAT_IN_* and HAT_OUT_*).
 */
#define BUSTRACE_SIZE 4096u
uint32_t bustrace_head(void);
void     bustrace_entry(uint32_t i, uint64_t *t, uint32_t *v);

/*
 * What sd2iec is doing, for the OLED (core 2). Core 1 fills this in through
 * sd2iec's own display hooks (rpi/display.c); texts are already ASCII.
 */
#define SD2IEC_NAME_LEN 16
#define SD2IEC_TEXT_LEN 21

struct sd2iec_status {
    uint8_t  address;                   /* device number */
    uint8_t  part;                      /* partition (0 = first) */
    uint8_t  file_write;                /* last file: 0 = read, 1 = write */
    uint32_t file_count;                /* counts up with every opened file */
    char     dir[SD2IEC_NAME_LEN + 1];  /* current directory, "" = root */
    char     file[SD2IEC_NAME_LEN + 1]; /* last file */
    char     cmd[SD2IEC_TEXT_LEN + 1];  /* last DOS command (channel 15) */
    char     msg[SD2IEC_TEXT_LEN + 1];  /* last error channel message */
    uint32_t dir_count;                 /* counts up with every directory change */
};

/* Consistent copy of the status; may be called from any core */
void sd2iec_get_status(struct sd2iec_status *out);

/*
 * OLED font (rpi/charrom.c): the C64 character ROM from the SD card root
 * (CHARGEN, 4 KB), read once by core 1. Core 2 uses it once the state is
 * CHARGEN_LOADED; texts may then also hold PETSCII graphics (bytes 0x80+).
 */
enum chargen_state { CHARGEN_UNTRIED, CHARGEN_LOADED, CHARGEN_MISSING };
extern uint8_t rpi_chargen[4096];
extern volatile uint8_t rpi_chargen_state;
void rpi_load_chargen(void);

/* sd2iec's debug output (core 1), for core 0: next character or -1 */
int sd2iec_log_getc(void);

/*
 * SD card browser (rpi/browser.c). Core 2 asks, core 1 does the work with
 * sd2iec's own directory functions while the IEC bus is idle, and publishes
 * the sorted list of the current directory (directories first).
 */
#define BROWSE_NAME_LEN 64              /* long FAT names (rpi/longname.c) */

enum browse_kind { BROWSE_FILE, BROWSE_DIR, BROWSE_IMAGE };
enum browse_op   { BROWSE_LIST = 1, BROWSE_ENTER, BROWSE_UP, BROWSE_SET_ROM };
enum browse_res  { BROWSE_OK, BROWSE_FILE_READY, BROWSE_FAILED, BROWSE_EMU, BROWSE_ROM_SET };

struct browse_entry {
    char    name[BROWSE_NAME_LEN + 1];  /* ASCII */
    char    type[4];                    /* "DIR", "D64", "PRG", ... */
    uint8_t kind;                       /* enum browse_kind */
};

struct browse_info {
    uint32_t gen;                       /* +1 for every finished request */
    uint32_t count;                     /* entries in the list */
    uint32_t cursor;                    /* where the cursor belongs now */
    uint32_t dir_count;                 /* sd2iec_status.dir_count of the list */
    uint8_t  result;                    /* enum browse_res of the last request */
    uint8_t  at_root;                   /* root of the SD card */
    uint8_t  truncated;                 /* directory had more entries */
    char     title[BROWSE_NAME_LEN + 1];
    char     note[SD2IEC_TEXT_LEN + 1];   /* why a request failed, "" = see sd2iec */
};

/* Core 2: queue a request (index/gen only for BROWSE_ENTER). 0 = queue full. */
int  browse_request(enum browse_op op, uint32_t index, uint32_t gen);
/* Core 2: info, plus up to n entries from 'first' (out may be NULL if n = 0) */
void browse_snapshot(struct browse_info *info, unsigned first, unsigned n,
                     struct browse_entry *out);

/*
 * 1541 emulation (rpi/emu1541.c, src/emu/): a D64 selected in the browser
 * runs in a cycle-exact 1541 on core 1, in place of sd2iec, until Exit.
 * The DOS ROM comes from the SD card root: DOS1541 (16 KB).
 */
struct emu1541_status {
    uint8_t  active;                    /* emulation running */
    uint8_t  track;                     /* head position: track 1..42 */
    uint8_t  halftrack;                 /* head between two tracks */
    uint8_t  motor, led;
    uint32_t late;                      /* cycles that started too late */
    uint32_t late_max_ns;               /* largest delay of such a cycle */
    uint32_t seconds;                   /* emulated drive time */
    uint8_t  swapping;                  /* disk being changed */
    uint8_t  saving;                    /* writing changed sectors to the SD card */
    char     rom[13];                   /* drive ROM in use (lowercase 8.3 name) */
    char     name[BROWSE_NAME_LEN + 1];
};

void emu1541_get_status(struct emu1541_status *out);   /* any core */
/* Core 0: print the emulated drive's state on the console (key 'd') */
void emu1541_dump(void (*print)(const char *fmt, ...));
/* Core 0: snapshot the drive the first time it reaches pc (0 = off), key 'w' */
void emu1541_set_watch(uint16_t pc);
/* Core 0: log the registers each time the drive reaches one of these
   addresses (up to 4; n = 0: off), key 'l'; print the log, key 'p' */
void emu1541_set_log(const uint16_t *pcs, unsigned n);
void emu1541_print_log(void (*print)(const char *fmt, ...));
/* Bus trace during the 1541 emulation (off by default: it loads the bus) */
extern volatile uint8_t rpi_trace_in_emu;
void emu1541_request_stop(void);                       /* core 2: Exit */
/* Core 0, from its main loop: SD card work for the emulation (saving,
   reading the next D64 at a disk change), so the drive never pauses */
void emu1541_worker(void);
/* Core 2: change the disk to browser list entry 'index' (a D64 in the same
   directory) while the emulation keeps running */
void emu1541_request_swap(uint32_t index);

#endif
