// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// disk1541.c - D64 to GCR (see disk1541.h)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Track layout as the 1541 DOS formats it:
//   per sector:  SYNC (5 x $FF)
//                header: $08, checksum, sector, track, id2, id1, $0F, $0F
//                        (8 bytes -> 10 GCR bytes)
//                gap: 9 x $55
//                SYNC (5 x $FF)
//                data: $07, 256 bytes, checksum, $00, $00
//                        (260 bytes -> 325 GCR bytes)
//                gap: the rest of the track spread over the sectors ($55)
//   track length: 7692 / 7142 / 6666 / 6250 bytes for zones 3 / 2 / 1 / 0
//   (bit rate 307692 / 285714 / 266667 / 250000 per second at 300 rpm).

#include <stdint.h>
#include <string.h>
#include "disk1541.h"

static const uint8_t gcr_nibble[16] = {
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
};

static const uint16_t track_bytes[4] = { 6250, 6666, 7142, 7692 };

static unsigned sectors_of_track(unsigned track)
{
    return track <= 17 ? 21 : track <= 24 ? 19 : track <= 30 ? 18 : 17;
}

/* Bit writer into one track */
struct bits {
    uint8_t *p;
    uint32_t pos;                       /* in bits */
};

static void put_bits(struct bits *b, uint32_t v, unsigned n)
{
    while (n--) {
        uint32_t byte = b->pos >> 3;
        uint8_t mask = (uint8_t)(0x80 >> (b->pos & 7));
        if ((v >> n) & 1)
            b->p[byte] |= mask;
        else
            b->p[byte] &= (uint8_t)~mask;
        b->pos++;
    }
}

static void put_raw(struct bits *b, uint8_t v, unsigned count)
{
    while (count--)
        put_bits(b, v, 8);
}

/* GCR: every 4 data bytes become 5 bytes (each nibble 5 bits) */
static void put_gcr(struct bits *b, const uint8_t *src, unsigned len)
{
    for (unsigned i = 0; i < len; i++) {
        put_bits(b, gcr_nibble[src[i] >> 4], 5);
        put_bits(b, gcr_nibble[src[i] & 15], 5);
    }
}

void (*disk_pace)(void);

int disk_from_d64(struct disk1541 *disk, const uint8_t *d64, uint32_t size)
{
    unsigned tracks;

    if (size == 174848 || size == 175531)
        tracks = 35;
    else if (size == 196608 || size == 197376)
        tracks = 40;
    else
        return -1;

    memset(disk, 0, sizeof(*disk));

    /* Disk ID from the BAM (track 18, sector 0, bytes $A2/$A3) */
    const uint8_t *bam = d64 + 357 * 256;
    uint8_t id1 = bam[0xa2], id2 = bam[0xa3];

    uint32_t offset = 0;
    for (unsigned t = 1; t <= tracks; t++) {
        unsigned zone = disk_zone_of_track(t), ns = sectors_of_track(t);
        unsigned len = track_bytes[zone];
        unsigned ht = (t - 1) * 2;
        struct bits b = { disk->data[ht / 2], 0 };

        /* Gap after each data block: whatever is left, spread evenly */
        unsigned per_sector = 5 + 10 + 9 + 5 + 325;
        unsigned gap = (len - ns * per_sector) / ns;

        for (unsigned s = 0; s < ns; s++) {
            const uint8_t *sec = d64 + offset + s * 256;
            uint8_t hdr[8] = { 0x08, (uint8_t)(s ^ t ^ id2 ^ id1), (uint8_t)s, (uint8_t)t,
                               id2, id1, 0x0f, 0x0f };
            uint8_t blk[260];
            uint8_t sum = 0;

            blk[0] = 0x07;
            for (unsigned i = 0; i < 256; i++) {
                blk[1 + i] = sec[i];
                sum ^= sec[i];
            }
            blk[257] = sum;
            blk[258] = blk[259] = 0;

            put_raw(&b, 0xff, 5);
            put_gcr(&b, hdr, 8);
            put_raw(&b, 0x55, 9);
            put_raw(&b, 0xff, 5);
            put_gcr(&b, blk, 260);
            put_raw(&b, 0x55, gap);
        }
        /* Fill up to the track length */
        while (b.pos < len * 8)
            put_bits(&b, 0x55, 8);

        disk->len[ht]  = (uint16_t)len;
        disk->zone[ht] = (uint8_t)zone;
        offset += ns * 256;
        if (disk_pace)
            disk_pace();
    }
    disk->tracks = (uint8_t)tracks;
    disk->present = 1;
    return 0;
}

/* ----- back to D64 ----- */

long disk_d64_offset(unsigned track, unsigned sector)
{
    if (track < 1 || track > 40 || sector >= sectors_of_track(track))
        return -1;
    long o = 0;
    for (unsigned t = 1; t < track; t++)
        o += sectors_of_track(t);
    return (o + sector) * 256;
}

/* 5-bit GCR code to nibble, 0xff = not a valid code */
static uint8_t gcr_value(unsigned code)
{
    for (unsigned i = 0; i < 16; i++)
        if (gcr_nibble[i] == code)
            return (uint8_t)i;
    return 0xff;
}

struct reader {
    const uint8_t *p;
    uint32_t bits;                      /* track length in bits */
};

static unsigned get_bit(const struct reader *r, uint32_t pos)
{
    pos %= r->bits;
    return (r->p[pos >> 3] >> (7 - (pos & 7))) & 1;
}

/* Decode n GCR bytes starting at bit pos; -1 if a code is invalid */
static int get_gcr(const struct reader *r, uint32_t pos, uint8_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uint8_t v = 0;
        for (unsigned half = 0; half < 2; half++) {
            unsigned code = 0;
            for (unsigned b = 0; b < 5; b++)
                code = code << 1 | get_bit(r, pos++);
            uint8_t nib = gcr_value(code);
            if (nib == 0xff)
                return -1;
            v = (uint8_t)(v << 4 | nib);
        }
        out[i] = v;
    }
    return 0;
}

int disk_to_d64(struct disk1541 *disk, uint8_t *d64, uint32_t size, uint8_t *changed)
{
    uint8_t tracks[DISK_HALFTRACKS / 2];

    memcpy(tracks, disk->dirty, sizeof(tracks));
    memset(disk->dirty, 0, sizeof(disk->dirty));
    disk->written = 0;
    return disk_tracks_to_d64(disk, tracks, d64, size, changed);
}

int disk_tracks_to_d64(const struct disk1541 *disk, const uint8_t *tracks,
                       uint8_t *d64, uint32_t size, uint8_t *changed)
{
    int count = 0;

    for (unsigned t = 1; t <= disk->tracks; t++) {
        if (!tracks[t - 1])
            continue;
        unsigned ht = (t - 1) * 2;
        struct reader r = { disk->data[ht / 2], disk->len[ht] * 8u };
        unsigned ones = 0;
        int sector = -1;                /* from the last good header */
        uint32_t header_at = 0;

        /* One full turn, plus enough to finish a block that wraps around */
        for (uint32_t i = 0; i < r.bits + 3000; i++) {
            if (get_bit(&r, i)) {
                ones++;
                continue;
            }
            if (ones < 10) {
                ones = 0;
                continue;
            }
            ones = 0;                   /* a SYNC ended: a block starts at i */

            uint8_t blk[260];
            if (get_gcr(&r, i, blk, 1))
                continue;
            if (blk[0] == 0x08) {
                sector = -1;
                if (!get_gcr(&r, i, blk, 8) && blk[3] == t &&
                    (blk[1] ^ blk[2] ^ blk[3] ^ blk[4] ^ blk[5]) == 0 &&
                    blk[2] < sectors_of_track(t)) {
                    sector = blk[2];
                    header_at = i;
                }
            } else if (blk[0] == 0x07 && sector >= 0 && i - header_at < 2000) {
                if (!get_gcr(&r, i, blk, 258)) {
                    uint8_t sum = 0;
                    for (unsigned k = 1; k <= 256; k++)
                        sum ^= blk[k];
                    long off = disk_d64_offset(t, (unsigned)sector);
                    if (sum == blk[257] && off >= 0 && (uint32_t)off + 256 <= size &&
                        memcmp(d64 + off, blk + 1, 256)) {
                        memcpy(d64 + off, blk + 1, 256);
                        if (!changed[off / 256])
                            count++;
                        changed[off / 256] = 1;
                    }
                }
                sector = -1;
            }
        }
        if (disk_pace)
            disk_pace();
    }
    return count;
}
