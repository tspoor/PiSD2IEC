// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// browser.c - SD card browser for the OLED, the core 1 side
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// The buttons and the OLED belong to core 2 (ui_core.c), but the SD card
// and FAT belong to sd2iec on core 1 and may only be used from there. So
// core 2 sends requests through a queue, and core 1 handles them while the
// IEC bus is idle, through the hook sd2iec has for external displays:
// display_intrq_active() is true while requests are waiting, sd2iec's tick
// then sets KEY_DISPLAY, and sd2iec's main loop calls display_service()
// when no ATN is active (iec.c, BUS_IDLE). Should the C64 assert ATN
// meanwhile, core 3 still answers it (DATA low), as with any slow operation.
//
// Everything goes through sd2iec's own functions (opendir/readdir/chdir/
// update_current_dir/file_open), so the browser and the C64 always see the
// same current directory:
//   - entering a directory or disk image is a CD, as "CD:NAME" would do;
//   - Exit is "CD:_" (one level up, or out of a disk image);
//   - selecting a file opens it once and closes it again, which makes it
//     sd2iec's "previous file", so LOAD"*",8,1 on the C64 loads exactly
//     that file;
//   - selecting a D64 runs it in the cycle-exact 1541 emulation instead
//     (rpi/emu1541.c) until Exit; other images (D71/D81/DNP/M2I) are
//     entered with sd2iec.
//
// The result (sorted list, directories first) is published with a sequence
// number (seqlock) for core 2.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "errormsg.h"
#include "doscmd.h"
#include "fatops.h"
#include "fileops.h"
#include "parser.h"
#include "wrapops.h"
#include "lib/ring.h"
#include "storage.h"
#include "petscii.h"
#include "emu1541.h"
#include "longname.h"

#define BROWSE_MAX   4096u              /* entries per directory */
#define STACK_DEPTH  32u                /* remembered levels for Exit */

/* Requests from core 2 (one writer: core 2, one reader: core 1) */
static struct ring q_req;

/* Core 1 only */
static cbmdirent_t         dents[BROWSE_MAX];
static struct browse_entry work[BROWSE_MAX];
static uint16_t            order[BROWSE_MAX];

/* Published for core 2, under 'seq' */
static struct browse_entry list[BROWSE_MAX];
static uint16_t            list_dent[BROWSE_MAX];   /* list index -> dents index */
static struct browse_info  info;
static uint32_t            seq;

/* Long names of FAT directories entered with the browser, for the title */
#define TITLE_CACHE 32u
static struct {
    uint8_t  part;
    uint32_t cluster;
    char     name[BROWSE_NAME_LEN + 1];
} titles[TITLE_CACHE];
static unsigned titles_next;

/* FAT short names of the directories/images we entered, for Exit */
static uint8_t  stack[STACK_DEPTH][sizeof(((cbmdirent_t *)0)->pvt.fat.realname)];
static unsigned stack_len;

static void publish_begin(void)
{
    __atomic_store_n(&seq, seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static void publish_end(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&seq, seq + 1, __ATOMIC_RELEASE);
}

/* ----- core 2 side ----- */

int browse_request(enum browse_op op, uint32_t index, uint32_t gen)
{
    return ring_put(&q_req, op, (index & 0xffffu) | (gen << 16), 0);
}

void browse_snapshot(struct browse_info *out, unsigned first, unsigned n,
                     struct browse_entry *entries)
{
    for (;;) {
        uint32_t s1 = __atomic_load_n(&seq, __ATOMIC_ACQUIRE);
        if (s1 & 1)
            continue;                   /* core 1 is publishing right now */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        *out = info;
        for (unsigned i = 0; i < n && first + i < out->count; i++)
            entries[i] = list[first + i];
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (__atomic_load_n(&seq, __ATOMIC_ACQUIRE) == s1)
            return;
    }
}

/* Core 3 (sd2iec's tick): requests waiting, or a bus reset to handle? */
unsigned int display_intrq_active(void)
{
    return ring_pending(&q_req) || rpi_reset_seen ||
           rpi_chargen_state == CHARGEN_UNTRIED;
}

/* ----- core 1 side ----- */

static const char *const type_names[8] = {
    "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???"
};

static int on_fat(uint8_t part)
{
    return partition[part].fop == &fatops;
}

static void classify(const cbmdirent_t *d, int fat, struct browse_entry *e)
{
    unsigned t = d->typeflags & EXT_TYPE_MASK;

    rpi_copy_pet(e->name, BROWSE_NAME_LEN, d->name, CBM_NAME_LENGTH, 1);

    if (t == TYPE_DIR) {
        e->kind = BROWSE_DIR;
        memcpy(e->type, "DIR", 4);
    } else if (fat && check_imageext((uint8_t *)d->pvt.fat.realname) != IMG_UNKNOWN) {
        /* Disk image (D64/D71/D81/DNP/M2I): the extension of the FAT name */
        const uint8_t *ext = (const uint8_t *)strrchr((const char *)d->pvt.fat.realname, '.');
        e->kind = BROWSE_IMAGE;
        for (unsigned i = 0; i < 3; i++) {
            char c = ext ? (char)ext[1 + i] : '?';
            e->type[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        e->type[3] = 0;
    } else {
        e->kind = BROWSE_FILE;
        memcpy(e->type, t < 8 ? type_names[t] : (t == TYPE_NAT ? "NAT" : "???"), 4);
    }
}

/* "NAME.EXT" (FatFs) -> 11 bytes "NAME    EXT", upper case */
static void sfn_raw(const uint8_t *name, uint8_t *raw)
{
    memset(raw, ' ', 11);
    unsigned i = 0, n = 0;
    for (; name[i] && name[i] != '.' && n < 8; i++)
        raw[n++] = name[i];
    while (name[i] && name[i] != '.')
        i++;
    if (name[i] == '.')
        for (i++, n = 8; name[i] && n < 11; i++)
            raw[n++] = name[i];
    for (n = 0; n < 11; n++)
        if (raw[n] >= 'a' && raw[n] <= 'z')
            raw[n] = (uint8_t)(raw[n] - 'a' + 'A');
    if (raw[0] == 0xE5)
        raw[0] = 0x05;                  /* as stored on the disk */
}

/*
 * Long names: sd2iec shows a FAT name longer than 16 characters as its 8.3
 * name. For the OLED, put the long name in instead (rpi/longname.c). The
 * directory is read in the same order as sd2iec's readdir did, so the
 * entries are matched one after the other.
 */
struct lfn_match {
    unsigned next, count;
};

static void lfn_found(const uint8_t *sfn, const char *lfn, void *ctx)
{
    struct lfn_match *m = ctx;
    uint8_t raw[11];

    if (m->next >= m->count)
        return;
    sfn_raw(dents[m->next].pvt.fat.realname, raw);
    for (unsigned i = 0; i < 11; i++) {
        uint8_t c = sfn[i];
        if (c >= 'a' && c <= 'z')
            c = (uint8_t)(c - 'a' + 'A');
        if (c != raw[i])
            return;                     /* not in the list (hidden, . or ..) */
    }
    if (lfn && strlen(lfn) > CBM_NAME_LENGTH && dents[m->next].opstype == OPSTYPE_FAT) {
        strncpy(work[m->next].name, lfn, BROWSE_NAME_LEN);
        work[m->next].name[BROWSE_NAME_LEN] = 0;
    }
    m->next++;
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* Directories first, then by name (case-insensitive) */
static int compare(const void *a, const void *b)
{
    const struct browse_entry *x = &work[*(const uint16_t *)a];
    const struct browse_entry *y = &work[*(const uint16_t *)b];

    if ((x->kind == BROWSE_DIR) != (y->kind == BROWSE_DIR))
        return x->kind == BROWSE_DIR ? -1 : 1;
    for (unsigned i = 0; ; i++) {
        int cx = lower((unsigned char)x->name[i]);
        int cy = lower((unsigned char)y->name[i]);
        if (cx != cy || !cx)
            return cx - cy;
    }
}

/*
 * Read the current directory, sort it and publish it. 'select' is the FAT
 * short name of the entry to put the cursor on (after Exit), or NULL.
 */
static void build_list(const uint8_t *select, uint8_t result)
{
    path_t path;
    dh_t dh;
    cbmdirent_t d;
    unsigned n = 0, truncated = 0, cursor = 0;

    path.part = current_part;
    path.dir  = partition[current_part].current_dir;
    int fat = on_fat(path.part);

    if (!opendir(&dh, &path)) {
        while (readdir(&dh, &d) == 0) {
            if (d.typeflags & FLAG_HIDDEN)
                continue;
            if (n == BROWSE_MAX) {
                truncated = 1;
                break;
            }
            dents[n] = d;
            classify(&d, fat, &work[n]);
            order[n] = (uint16_t)n;
            n++;
        }
    }
    if (fat && n) {
        struct lfn_match m = { 0, n };
        longname_scan(&partition[path.part].fatfs, path.dir.fat, lfn_found, &m);
    }
    qsort(order, n, sizeof(order[0]), compare);

    if (select && fat)
        for (unsigned i = 0; i < n; i++)
            if (!strcmp((const char *)dents[order[i]].pvt.fat.realname, (const char *)select)) {
                cursor = i;
                break;
            }

    struct sd2iec_status st;
    sd2iec_get_status(&st);

    publish_begin();
    for (unsigned i = 0; i < n; i++) {
        list[i]      = work[order[i]];
        list_dent[i] = order[i];
    }
    info.count     = n;
    info.cursor    = cursor;
    info.dir_count = st.dir_count;
    info.result    = result;
    info.at_root   = fat && path.dir.fat == 0;
    info.truncated = (uint8_t)truncated;
    if (info.at_root) {
        strcpy(info.title, "/");
    } else {
        const char *t = st.dir[0] ? st.dir : "?";
        for (unsigned i = 0; fat && i < TITLE_CACHE; i++)
            if (titles[i].name[0] && titles[i].part == path.part &&
                titles[i].cluster == path.dir.fat)
                t = titles[i].name;
        strncpy(info.title, t, BROWSE_NAME_LEN);
    }
    info.title[BROWSE_NAME_LEN] = 0;
    info.gen++;
    publish_end();
}

/* Finish a request without a new list (e.g. an error) */
static void publish_result(uint8_t result)
{
    publish_begin();
    info.result = result;
    info.gen++;
    publish_end();
}

static void publish_note(const char *note)
{
    publish_begin();
    strncpy(info.note, note, sizeof(info.note) - 1);
    publish_end();
}

static void emu_started(void)
{
    publish_result(BROWSE_EMU);
}

static void enter(uint32_t index, uint32_t gen)
{
    /* The list must still be the one core 2 looked at */
    if (gen != (info.gen & 0xffffu) || index >= info.count) {
        build_list(NULL, BROWSE_FAILED);
        return;
    }

    cbmdirent_t *d = &dents[list_dent[index]];
    uint8_t kind = list[index].kind;
    path_t path;
    path.part = current_part;
    path.dir  = partition[current_part].current_dir;
    int fat = on_fat(path.part);

    publish_note("");

    if (kind == BROWSE_IMAGE && !strcmp(list[index].type, "D64")) {
        /* Real 1541 emulation; returns when the user presses Exit */
        char note[SD2IEC_TEXT_LEN + 1] = "";
        path.dir = partition[current_part].current_dir;
        if (emu1541_run_d64(&path, d, note, emu_started)) {
            publish_note(note);
            publish_result(BROWSE_FAILED);
            return;
        }
        build_list(NULL, BROWSE_OK);
        return;
    }

    if (kind == BROWSE_DIR || kind == BROWSE_IMAGE) {
        /* Like do_chdir() in doscmd.c, but with the entry we already have */
        previous_file_dirent.name[0] = 0;
        if (chdir(&path, d)) {
            publish_result(BROWSE_FAILED);
            return;
        }
        update_current_dir(&path);
        if (fat && stack_len < STACK_DEPTH)
            memcpy(stack[stack_len++], d->pvt.fat.realname, sizeof(stack[0]));
        if (fat && kind == BROWSE_DIR && on_fat(path.part)) {
            /* Remember the long name of this directory for the title */
            unsigned t = titles_next++ % TITLE_CACHE;
            titles[t].part    = path.part;
            titles[t].cluster = path.dir.fat;
            memcpy(titles[t].name, list[index].name, sizeof(titles[t].name));
        }
        build_list(NULL, BROWSE_OK);
        return;
    }

    /* A file: open it once on secondary 0 and close it again. sd2iec then
       remembers it as the previous file, for LOAD"*". */
    unsigned len = 0;
    while (len < CBM_NAME_LENGTH && d->name[len])
        len++;
    memcpy(command_buffer, d->name, len);
    command_length = (uint8_t)len;
    file_open(0);
    command_length = 0;

    buffer_t *buf = find_buffer(0);
    if (buf)
        cleanup_and_free_buffer(buf);

    publish_result(buf && previous_file_dirent.name[0] ? BROWSE_FILE_READY : BROWSE_FAILED);
}

static void up(void)
{
    path_t path;
    path.part = current_part;
    path.dir  = partition[current_part].current_dir;

    if (on_fat(path.part) && path.dir.fat == 0) {
        build_list(NULL, BROWSE_OK);    /* already at the root */
        return;
    }

    /* Like "CD:_" in do_chdir() */
    previous_file_dirent.name[0] = 0;
    if (chdir(&path, NULL)) {
        publish_result(BROWSE_FAILED);
        return;
    }
    update_current_dir(&path);
    build_list(stack_len ? stack[--stack_len] : NULL, BROWSE_OK);
}

cbmdirent_t *browse_d64_dent(unsigned index)
{
    if (index >= info.count || list[index].kind != BROWSE_IMAGE ||
        strcmp(list[index].type, "D64"))
        return 0;
    return &dents[list_dent[index]];
}

/* Add on a file: use it as the 1541 ROM (rpi/emu1541.c) */
static void set_rom(uint32_t index, uint32_t gen)
{
    if (gen != (info.gen & 0xffffu) || index >= info.count || list[index].kind != BROWSE_FILE) {
        publish_note("Not a ROM file");
        publish_result(BROWSE_FAILED);
        return;
    }
    path_t path;
    path.part = current_part;
    path.dir  = partition[current_part].current_dir;
    char note[SD2IEC_TEXT_LEN + 1] = "";
    int r = emu1541_set_rom(&path, &dents[list_dent[index]], note);
    publish_note(note);
    publish_result(r < 0 ? BROWSE_FAILED : BROWSE_ROM_SET);
}

/* Called by sd2iec on core 1 when the bus is idle and requests are waiting */
void display_service(void)
{
    struct msg m;

    if (rpi_chargen_state == CHARGEN_UNTRIED)
        rpi_load_chargen();             /* OLED font, once (rpi/charrom.c) */

    /*
     * IEC RESET: a real SD2IEC usually resets with the bus. Here sd2iec
     * keeps running (and keeps its directory), but closes all files the C64
     * had open and shows the power-on message, as after a reset.
     */
    if (rpi_reset_seen) {
        rpi_reset_seen = 0;
        free_multiple_buffers(FMB_USER_CLEAN);
        set_error(ERROR_DOSVERSION);
    }

    while (ring_get(&q_req, &m)) {
        switch (m.type) {
        case BROWSE_LIST:
            build_list(NULL, BROWSE_OK);
            break;
        case BROWSE_ENTER:
            enter(m.arg & 0xffffu, m.arg >> 16);
            break;
        case BROWSE_UP:
            up();
            break;
        case BROWSE_SET_ROM:
            set_rom(m.arg & 0xffffu, m.arg >> 16);
            break;
        default:
            break;
        }
    }
}
