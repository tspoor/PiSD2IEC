// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// drive1541.h - the 1541 disk drive: 6502, two 6522s, 2 KB RAM, 16 KB ROM
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Memory map (address lines A13/A14 are not decoded, so $0000-$1FFF
// repeats up to $7FFF; the ROM appears at $8000 and at $C000):
//   $0000-$07FF  RAM
//   $1800-$180F  VIA1: serial (IEC) bus, mirrored up to $1BFF
//   $1C00-$1C0F  VIA2: drive mechanics, mirrored up to $1FFF
//   $8000-$FFFF  DOS ROM (16 KB, twice)
//
// VIA1 port B (IEC, inputs inverted by the 1541's 7406/7414):
//   PB0 DATA in   PB1 DATA out  PB2 CLK in   PB3 CLK out
//   PB4 ATN ack   PB5/6 device number jumpers   PB7 ATN in;  CA1 = ATN in
// The 1541 also has hardware that pulls DATA low as long as ATN in and
// ATN ack differ (the automatic ATN acknowledge, UD3 XOR gate).
//
// VIA2 (see drive1541.c for the disk side, reading and writing):
//   PB0/1 stepper phase  PB2 motor  PB3 LED  PB4 write protect (in)
//   PB5/6 bit rate zone  PB7 SYNC (in, 0 = sync)
//   PA = data byte from/to the head, CA1 = BYTE READY, CA2 = SO enable,
//   CB2 = head mode (1 = read)
//
// The platform supplies three hooks per clock cycle:
//   drive_platform_wait()   wait for this cycle's moment in real time;
//   drive_platform_sample() sample the bus into iec_in;
//   drive_platform_output() put iec_out on the bus.
// A read cycle samples first and then reads; a write cycle to VIA1 drives
// the new line levels right after the write and samples afterwards, so the
// drive's response reaches the bus as early as possible.

#ifndef DRIVE1541_H
#define DRIVE1541_H

#include <stdint.h>
#include "cpu6502.h"
#include "via6522.h"
#include "disk1541.h"

/* IEC lines, as bits: 1 = line pulled low (asserted) */
#define IEC_L_ATN   0x01
#define IEC_L_CLK   0x02
#define IEC_L_DATA  0x04

#define DRIVE_LOG 4096u

struct drive_log {
    uint64_t cycle;
    uint16_t pc;
    uint8_t  a, x, y, p;
};

struct drive1541 {
    struct cpu6502 cpu;
    struct via6522 via1, via2;
    uint8_t  ram[2048];
    const uint8_t *rom;                 /* 16 KB */
    uint8_t  device;                    /* 8..11 (jumpers) */
    uint8_t  iec_in;                    /* bus lines seen on the cable (IEC_L_*) */
    uint8_t  iec_out;                   /* lines the drive pulls low (IEC_L_CLK/DATA) */
    uint8_t  last_bus;                  /* last value on the data bus (open bus) */

    /* Disk mechanics (VIA2) */
    struct disk1541 *disk;              /* inserted disk, or 0 */
    uint8_t  write_protect;             /* 1: the disk has no write notch */
    uint32_t sensor_dark;               /* cycles the disk itself blocks the
                                           write protect sensor (inserting) */
    uint8_t  wshift;                    /* write mode: byte going to the head */
    uint8_t  ht;                        /* head position: half track 0..83 */
    uint8_t  phase;                     /* last stepper phase (PB0/1) */
    uint8_t  frac;                      /* quarter cycles towards the next bit */
    uint32_t bitpos;                    /* position on the track, in bits */
    uint16_t shift;                     /* last 10 bits read (SYNC detection) */
    uint8_t  byte_bits;                 /* bits since SYNC or the last byte */
    uint8_t  byte;                      /* byte being assembled */
    uint8_t  sync;                      /* head is over a SYNC mark */
    uint8_t  ready_low;                 /* cycles BYTE READY stays low */

    /* For debugging: recent instruction addresses and counters */
    uint16_t pc_hist[32];
    uint8_t  pc_hist_pos;
    uint32_t syncs, bytes, steps;

    /* For debugging: the first time the CPU reaches watch_pc, the whole
       drive is copied into *watch (if set) */
    uint16_t watch_pc;
    uint8_t  watch_hit;
    struct drive1541 *watch;

    /* For debugging: each time the CPU is about to run the instruction at
       one of log_pc[0..log_pcs-1], the registers go into log[] (a ring of
       DRIVE_LOG entries; log_n counts all entries ever made) */
    uint16_t log_pc[4];
    uint8_t  log_pcs;
    uint32_t log_n;
    struct drive_log *log;
};

void drive1541_init(struct drive1541 *d, const uint8_t *rom16k, uint8_t device);
void drive1541_reset(struct drive1541 *d);
/* Run whole instructions until at least 'cycles' more cycles have passed */
void drive1541_run(struct drive1541 *d, uint64_t cycles);
/* Insert (disk != 0) or remove (0) a disk; the drive only reads it */
void drive1541_insert(struct drive1541 *d, struct disk1541 *disk);

/* Platform hooks (see above) */
void drive_platform_wait(struct drive1541 *d);
void drive_platform_sample(struct drive1541 *d);
void drive_platform_output(struct drive1541 *d);

#endif
