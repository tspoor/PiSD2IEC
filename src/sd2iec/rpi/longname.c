// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// longname.c - full long file names of a FAT directory, for the OLED
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// sd2iec shows a FAT file under its long name only if that fits in a CBM
// name (16 characters); otherwise under its 8.3 name ("GHOSTN~1.D64").
// Its FatFs also returns long names of at most 20 characters. For the
// browser on the OLED we want the whole name, so this file reads the
// directory itself: sector by sector, straight from the card (disk_read),
// following the cluster chain in the FAT, and puts the long name entries
// (13 UCS-2 characters each, stored in front of the 8.3 entry) together.
// It only reads, and only for display: everything else keeps using
// sd2iec's own names.

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "diskio.h"
#include "ff.h"
#include "longname.h"

static uint8_t sector[512];

/* Next cluster from the FAT (FAT16/FAT32), or 0 at the end / on error */
static uint32_t next_cluster(FATFS *fs, uint32_t clust)
{
    uint32_t sect, off, next;

    if (fs->fs_type == FS_FAT32) {
        sect = fs->fatbase + clust / 128u;
        off  = (clust % 128u) * 4u;
    } else {
        sect = fs->fatbase + clust / 256u;
        off  = (clust % 256u) * 2u;
    }
    if (disk_read(fs->drive, sector, sect, 1) != RES_OK)
        return 0;
    if (fs->fs_type == FS_FAT32)
        next = (sector[off] | (uint32_t)sector[off + 1] << 8 |
                (uint32_t)sector[off + 2] << 16 | (uint32_t)sector[off + 3] << 24) & 0x0FFFFFFFu;
    else
        next = sector[off] | (uint32_t)sector[off + 1] << 8;
    if (next < 2 || next >= fs->max_clust)
        return 0;
    return next;
}

/* Checksum of an 8.3 name, as stored in its long name entries */
static uint8_t sfn_sum(const uint8_t *name)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]);
    return sum;
}

/* Character positions of the 13 UCS-2 characters in a long name entry */
static const uint8_t lfn_pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

int longname_scan(FATFS *fs, uint32_t dir_cluster,
                  void (*found)(const uint8_t *sfn, const char *lfn, void *ctx),
                  void *ctx)
{
    char    lfn[LONGNAME_MAX + 1];
    uint8_t lfn_sum = 0, lfn_ok = 0;
    uint32_t clust = 0, sect, left;     /* sectors left in this cluster/table */

    if (fs->fs_type != FS_FAT16 && fs->fs_type != FS_FAT32)
        return -1;                      /* FAT12: not worth it */

    if (dir_cluster == 0 && fs->fs_type == FS_FAT16) {
        sect = fs->dirbase;             /* fixed root directory */
        left = (fs->n_rootdir * 32u + 511u) / 512u;
    } else {
        clust = dir_cluster ? dir_cluster : fs->dirbase;
        sect  = (clust - 2) * fs->csize + fs->database;
        left  = fs->csize;
    }

    for (;;) {
        if (disk_read(fs->drive, sector, sect, 1) != RES_OK)
            return -1;
        for (unsigned i = 0; i < 512; i += 32) {
            const uint8_t *e = &sector[i];
            if (e[0] == 0)
                return 0;               /* end of the directory */
            if (e[0] == 0xE5) {         /* deleted */
                lfn_ok = 0;
                continue;
            }
            if ((e[11] & 0x3F) == 0x0F) {
                /* Long name entry: part (e[0] & 0x1f), last one first */
                unsigned part = e[0] & 0x1Fu;
                if (e[0] & 0x40) {      /* the last part: starts a new name */
                    memset(lfn, 0, sizeof(lfn));
                    lfn_sum = e[13];
                    lfn_ok  = 1;
                }
                if (!lfn_ok || part == 0 || e[13] != lfn_sum) {
                    lfn_ok = 0;
                    continue;
                }
                for (unsigned k = 0; k < 13; k++) {
                    unsigned pos = (part - 1) * 13u + k;
                    uint16_t c = (uint16_t)(e[lfn_pos[k]] | e[lfn_pos[k] + 1] << 8);
                    if (c == 0 || c == 0xFFFF || pos >= LONGNAME_MAX)
                        break;
                    lfn[pos] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
                }
                continue;
            }
            /* 8.3 entry (or volume label): the long name belongs to it if
               the checksum matches */
            if (!(e[11] & AM_VOL))
                found(e, lfn_ok && sfn_sum(e) == lfn_sum ? lfn : 0, ctx);
            lfn_ok = 0;
        }

        /* Next sector */
        sect++;
        if (--left == 0) {
            if (!clust)
                return 0;               /* end of the fixed root directory */
            clust = next_cluster(fs, clust);
            if (!clust)
                return 0;
            sect = (clust - 2) * fs->csize + fs->database;
            left = fs->csize;
        }
    }
}
