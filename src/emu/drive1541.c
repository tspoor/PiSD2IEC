// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// drive1541.c - the 1541: memory map, chips and wiring (see drive1541.h)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Every bus access of the 6502 is one clock cycle:
//   1. drive_platform_wait() and drive_platform_sample(): wait for this
//      cycle's real time, sample the IEC bus into iec_in (a write to VIA1
//      first drives the bus, then samples);
//   2. present the inputs to VIA1 (port B, CA1) and VIA2;
//   3. the CPU's read or write;
//   4. clock both VIAs once (via_tick) and turn the disk;
//   5. compute which IEC lines the drive pulls low (iec_out) and put them
//      on the bus at once (drive_platform_output()).
//
// The disk: while the motor (VIA2 PB2) runs, a bit passes under the head
// every 3.25 / 3.5 / 3.75 / 4 cycles, depending on the bit rate zone set
// in VIA2 PB5/6 (16 MHz / 4 / (16 - zone), as in the real drive). This is
// counted exactly in quarter cycles. Ten or more 1 bits in a row are a
// SYNC (VIA2 PB7 = 0) and restart the byte counter; every 8 bits after
// that make a byte: it is put on VIA2 port A, and BYTE READY sets the
// 6502's V flag through the SO pin (if VIA2 CA2 enables it) and gives an
// edge on VIA2 CA1. The stepper phases (PB0/1) move the head by half
// tracks. In write mode (CB2 low) the head writes instead: each byte that
// the CPU puts on port A goes onto the track bit by bit, as GCR, at the
// same rate (the DOS does the GCR encoding, as on the real drive).

#include <stdint.h>
#include <string.h>
#include "drive1541.h"

static struct drive1541 *D;             /* the drive the CPU core works on */

static void drive_inputs(struct drive1541 *d)
{
    uint8_t in = d->iec_in;

    /* VIA1 port B inputs: DATA in, CLK in, ATN in are 1 when the line is low */
    uint8_t pb = (uint8_t)(((in & IEC_L_DATA) ? 0x01 : 0) |
                           ((in & IEC_L_CLK)  ? 0x04 : 0) |
                           ((in & IEC_L_ATN)  ? 0x80 : 0) |
                           (((d->device - 8) & 3) << 5));
    via_set_pb(&d->via1, (uint8_t)(pb | 0x1a));   /* outputs read back via ORB */
    via_set_ca1(&d->via1, (in & IEC_L_ATN) != 0);

    /* VIA2 inputs: PB7 SYNC (0 = sync), PB4 write protect sensor: 1 when it
       sees light (no disk, or a disk with its notch open), 0 when dark
       (notch covered, or the disk passing while it goes in or out) */
    uint8_t pb2 = 0x6f;
    if (!d->sync)
        pb2 |= 0x80;
    if (d->sensor_dark)
        d->sensor_dark--;
    else if (!d->disk || !d->write_protect)
        pb2 |= 0x10;
    via_set_pb(&d->via2, pb2);
}

/* ----- disk mechanics ----- */

static void disk_byte_ready(struct drive1541 *d)
{
    via_set_pa(&d->via2, d->byte);
    if (via_ca2_out(&d->via2))          /* SO enabled: V flag */
        cpu6502_set_overflow(&d->cpu);
    via_set_ca1(&d->via2, 0);
    d->ready_low = 2;
}

static void disk_bit(struct drive1541 *d)
{
    uint8_t *trk = d->disk ? disk_track(d->disk, d->ht) : 0;
    uint32_t len = trk ? d->disk->len[d->ht] * 8u : 6250u * 8u;
    int bit = 0;

    if (d->bitpos >= len)
        d->bitpos = 0;

    if (!via_cb2_out(&d->via2)) {
        /*
         * Write mode (VIA2 CB2 low): the head writes the top bit of the
         * shift register; every 8 bits the next byte comes from port A and
         * BYTE READY asks the CPU for another one. No SYNC detection.
         */
        bit = d->wshift >> 7;
        d->wshift = (uint8_t)(d->wshift << 1);
        if (trk && !d->write_protect) {
            uint8_t mask = (uint8_t)(0x80 >> (d->bitpos & 7));
            if (bit)
                trk[d->bitpos >> 3] |= mask;
            else
                trk[d->bitpos >> 3] &= (uint8_t)~mask;
            d->disk->dirty[d->ht / 2] = 1;
            d->disk->written = 1;
        }
        d->bitpos++;
        d->shift = (uint16_t)(((d->shift << 1) | bit) & 0x3ff);
        d->sync = 0;
        if (++d->byte_bits == 8) {
            d->byte_bits = 0;
            d->wshift = via_pa_out(&d->via2);
            disk_byte_ready(d);
        }
        return;
    }

    if (trk)
        bit = (trk[d->bitpos >> 3] >> (7 - (d->bitpos & 7))) & 1;
    d->bitpos++;

    d->shift = (uint16_t)(((d->shift << 1) | bit) & 0x3ff);
    if (d->shift == 0x3ff && via_cb2_out(&d->via2)) {
        if (!d->sync)
            d->syncs++;
        d->sync = 1;                    /* SYNC: byte counter held at 0 */
        d->byte_bits = 0;
        return;
    }
    d->sync = 0;
    d->byte = (uint8_t)((d->byte << 1) | bit);
    if (++d->byte_bits == 8) {
        d->byte_bits = 0;
        d->bytes++;
        disk_byte_ready(d);
    }
}

static void disk_tick(struct drive1541 *d)
{
    uint8_t pb = d->via2.orb & d->via2.ddrb;

    /* Stepper: one phase forward or back moves the head half a track */
    uint8_t phase = pb & 3;
    if (phase != d->phase) {
        unsigned old = d->ht;
        if (((phase - d->phase) & 3) == 1 && d->ht < DISK_HALFTRACKS - 1)
            d->ht++;
        else if (((phase - d->phase) & 3) == 3 && d->ht > 0)
            d->ht--;
        d->phase = phase;
        if (d->ht != old)
            d->steps++;
        if (d->ht != old && d->disk) {
            /* Keep the angle: scale the position to the new track length */
            uint32_t ol = d->disk->len[old] ? d->disk->len[old] : 6250;
            uint32_t nl = d->disk->len[d->ht] ? d->disk->len[d->ht] : 6250;
            d->bitpos = (uint32_t)((uint64_t)d->bitpos * nl / ol);
        }
    }

    if (d->ready_low && !--d->ready_low)
        via_set_ca1(&d->via2, 1);

    if (!(pb & 0x04))                   /* motor off: the disk stands still */
        return;
    unsigned cell = 16 - ((pb >> 5) & 3);   /* bit time in quarter cycles */
    d->frac = (uint8_t)(d->frac + 4);
    if (d->frac >= cell) {
        d->frac = (uint8_t)(d->frac - cell);
        disk_bit(d);
    }
}

static void drive_outputs(struct drive1541 *d)
{
    /* Only bits configured as outputs drive the 7406 */
    uint8_t pb = d->via1.orb & d->via1.ddrb;
    int atn_in  = (d->iec_in & IEC_L_ATN) != 0;
    int atn_ack = (pb & 0x10) != 0;
    uint8_t out = 0;

    if ((pb & 0x02) || (atn_in ^ atn_ack))
        out |= IEC_L_DATA;
    if (pb & 0x08)
        out |= IEC_L_CLK;
    d->iec_out = out;
}

static inline void cycle_sample(void)
{
    drive_platform_sample(D);
    drive_inputs(D);
}

static inline void cycle_end(void)
{
    via_tick(&D->via1);
    via_tick(&D->via2);
    disk_tick(D);
    drive_outputs(D);
    drive_platform_output(D);
}

/* ----- the bus, for cpu6502_impl.h ----- */

static inline uint8_t cpu_bus_read(uint16_t a)
{
    uint8_t v;

    drive_platform_wait(D);
    cycle_sample();
    if (a & 0x8000) {
        v = D->rom[a & 0x3fff];
    } else {
        uint16_t m = a & 0x1fff;
        if (m < 0x0800)
            v = D->ram[m];
        else if (m >= 0x1c00)
            v = via_read(&D->via2, m);
        else if (m >= 0x1800)
            v = via_read(&D->via1, m);
        else
            v = D->last_bus;            /* nothing there: open bus */
    }
    D->last_bus = v;
    cycle_end();
    return v;
}

static inline void cpu_bus_write(uint16_t a, uint8_t v)
{
    drive_platform_wait(D);
    if (!(a & 0x8000)) {
        uint16_t m = a & 0x1fff;
        if (m < 0x0800) {
            D->ram[m] = v;
        } else if (m >= 0x1c00) {
            via_write(&D->via2, m, v);
        } else if (m >= 0x1800) {
            /* Serial bus port: the new levels go out at once */
            via_write(&D->via1, m, v);
            drive_outputs(D);
            drive_platform_output(D);
        }
    }
    D->last_bus = v;
    cycle_sample();
    cycle_end();
}

static inline int cpu_irq_line(void)
{
    return via_irq(&D->via1) || via_irq(&D->via2);
}

#include "cpu6502_impl.h"

/* ----- API ----- */

void drive1541_init(struct drive1541 *d, const uint8_t *rom16k, uint8_t device)
{
    memset(d, 0, sizeof(*d));
    d->rom = rom16k;
    d->device = device;
    d->cpu.s = 0xfd;
    d->ht = 34;                         /* head on track 18 */
    d->via2.ca1 = 1;                    /* BYTE READY idles high */
}

void drive1541_insert(struct drive1541 *d, struct disk1541 *disk)
{
    d->disk = disk;
    d->sensor_dark = 200000;            /* 0.2 s: the disk passes the sensor */
}

void drive1541_reset(struct drive1541 *d)
{
    D = d;
    via_reset(&d->via1);
    via_reset(&d->via2);
    cpu6502_reset(&d->cpu);
}

void drive1541_run(struct drive1541 *d, uint64_t cycles)
{
    D = d;
    uint64_t end = d->cpu.cycles + cycles;
    while (d->cpu.cycles < end) {
        d->pc_hist[d->pc_hist_pos++ & 31] = d->cpu.pc;
        if (d->cpu.pc == d->watch_pc && d->watch && !d->watch_hit) {
            d->watch_hit = 1;
            *d->watch = *d;
        }
        if (d->log_pcs) {
            for (unsigned i = 0; i < d->log_pcs; i++) {
                if (d->cpu.pc != d->log_pc[i])
                    continue;
                struct drive_log *l = &d->log[d->log_n++ % DRIVE_LOG];
                l->cycle = d->cpu.cycles;
                l->pc = d->cpu.pc;
                l->a = d->cpu.a;
                l->x = d->cpu.x;
                l->y = d->cpu.y;
                l->p = d->cpu.p;
                break;
            }
        }
        cpu6502_step(&d->cpu);
    }
}
