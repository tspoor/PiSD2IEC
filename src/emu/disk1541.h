// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// disk1541.h - a 1541 disk as the head sees it: GCR bits per half track
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// A D64 holds only the sector contents. To let the emulated 1541 read it
// with its own DOS (or any custom drive code), each track is rebuilt as
// the bit stream a real disk has: per sector a SYNC, the header block, a
// gap, a SYNC, the data block and a gap, all GCR encoded, with the track
// length a 1541 writes at 300 rpm in that track's speed zone.
//
// Half tracks: index ht = (track - 1) * 2, so 0..83 covers tracks 1..42.
// Odd half tracks (between two tracks) have no data on a D64.

#ifndef DISK1541_H
#define DISK1541_H

#include <stdint.h>

#define DISK_HALFTRACKS   84
#define DISK_TRACK_BYTES  7928          /* longest track we store (G64 maximum) */

struct disk1541 {
    uint8_t  present;
    uint8_t  tracks;                    /* 35 or 40 */
    uint8_t  written;                   /* written to since the last disk_to_d64 */
    uint8_t  dirty[DISK_HALFTRACKS / 2]; /* per track: written to */
    uint16_t len[DISK_HALFTRACKS];      /* bytes per half track, 0 = no data */
    uint8_t  zone[DISK_HALFTRACKS];     /* speed zone it was written with */
    uint8_t  data[DISK_HALFTRACKS / 2][DISK_TRACK_BYTES];   /* whole tracks */
};

/* Speed zone (0..3) of a track as the 1541 DOS formats it */
static inline unsigned disk_zone_of_track(unsigned track)
{
    return track <= 17 ? 3 : track <= 24 ? 2 : track <= 30 ? 1 : 0;
}

/*
 * Build the disk from a D64 image (35 or 40 tracks, with or without the
 * error info block). Returns 0 on success, -1 if the size is not a D64.
 */
int disk_from_d64(struct disk1541 *disk, const uint8_t *d64, uint32_t size);

/* Pointer to the bits of half track ht, or 0 if it holds no data */
static inline uint8_t *disk_track(struct disk1541 *disk, unsigned ht)
{
    return (disk->present && !(ht & 1) && disk->len[ht]) ? disk->data[ht / 2] : 0;
}

/*
 * Bring what was written back into the D64: decode every track that was
 * written to (SYNC, header, data block, both checksums, as a drive reads)
 * and copy sectors that differ into d64. changed[] (one flag per sector,
 * in D64 order, 768 entries) marks them. Returns the number of changed
 * sectors. Sectors that do not decode cleanly are left as they were.
 */
int disk_to_d64(struct disk1541 *disk, uint8_t *d64, uint32_t size, uint8_t *changed);

/*
 * The same for the tracks marked in tracks[] (one flag per track), without
 * touching the disk's own flags: for decoding on another core while the
 * drive keeps running. A sector the drive is rewriting at that moment does
 * not decode cleanly and is left out; its track is marked dirty again.
 */
int disk_tracks_to_d64(const struct disk1541 *disk, const uint8_t *tracks,
                       uint8_t *d64, uint32_t size, uint8_t *changed);

/*
 * If set, called after each track by disk_from_d64 and disk_tracks_to_d64:
 * lets a caller on another core spread the work out, so it does not crowd
 * the core that runs the drive (shared cache and memory).
 */
extern void (*disk_pace)(void);

/* Byte offset of a sector in a D64, or -1 */
long disk_d64_offset(unsigned track, unsigned sector);

#endif
