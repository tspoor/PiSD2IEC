// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// drive_test.c - boot the emulated 1541 on the PC (needs a 1541 DOS ROM)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
//   drive_test ROMFILE [D64FILE]
//
// Runs two seconds of drive time with nothing on the bus and checks that
// the DOS initialised: its power-on message "CBM DOS V2.6 1541" in RAM and
// the serial bus released. Then a simulated C64 reads the error channel
// over the serial bus with the standard (kernal) protocol, which must give
// "73,CBM DOS V2.6 1541,00,00".
//
// With a D64: the C64 does LOAD"$",8 (read with the drive's own DOS from
// the GCR bit stream) and the directory is printed. Then it SAVEs a file:
// the DOS writes GCR to the tracks, which are decoded back into the D64,
// and the file must be there, byte for byte.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/emu/drive1541.h"
#include "../../src/emu/disk1541.h"

static uint64_t platform_cycles;
static struct drive1541 drive;
static uint8_t c64;                     /* lines the simulated C64 pulls low */

void drive_platform_wait(struct drive1541 *d)
{
    (void)d;
    platform_cycles++;
}

void drive_platform_sample(struct drive1541 *d)
{
    d->iec_in = c64 | d->iec_out;       /* open collector: wired AND */
}

void drive_platform_output(struct drive1541 *d)
{
    (void)d;                            /* bus_low() reads iec_out directly */
}

/* ----- a simulated C64 on the serial bus (times in us = drive cycles) ----- */

/* Report where a bus transaction failed */
static int bus_fail(const char *what)
{
    fprintf(stderr, "  bus: %s failed (drive PC $%04X, %.3f s)\n",
            what, drive.cpu.pc, drive.cpu.cycles / 1e6);
    return 0;
}

static int bus_low(uint8_t line) { return ((c64 | drive.iec_out) & line) != 0; }
static void run_us(unsigned us) { drive1541_run(&drive, us); }

/* Wait until 'line' is low (want = 1) or released (want = 0) */
static int wait_line(uint8_t line, int want, unsigned timeout_us)
{
    for (unsigned t = 0; t < timeout_us; t++) {
        if (bus_low(line) == want)
            return 1;
        run_us(1);
    }
    return 0;
}

static int send_byte(uint8_t b, int eoi)
{
    c64 &= (uint8_t)~IEC_L_CLK;         /* talker ready */
    if (!wait_line(IEC_L_DATA, 0, 5000000))
        return 0;                       /* listener never got ready (5 s) */
    if (eoi) {                          /* wait for the EOI acknowledge */
        if (!wait_line(IEC_L_DATA, 1, 1000) || !wait_line(IEC_L_DATA, 0, 1000))
            return 0;
    }
    run_us(40);
    c64 |= IEC_L_CLK;
    for (int i = 0; i < 8; i++) {
        run_us(30);
        if (b & (1 << i)) c64 &= (uint8_t)~IEC_L_DATA; else c64 |= IEC_L_DATA;
        run_us(30);
        c64 &= (uint8_t)~IEC_L_CLK;     /* data valid */
        run_us(60);
        c64 |= IEC_L_CLK;
        c64 &= (uint8_t)~IEC_L_DATA;
    }
    return wait_line(IEC_L_DATA, 1, 1000);   /* frame acknowledge */
}

static int atn_bytes(const uint8_t *b, int n)
{
    c64 |= IEC_L_ATN | IEC_L_CLK;
    c64 &= (uint8_t)~IEC_L_DATA;
    run_us(1000);
    if (!bus_low(IEC_L_DATA))
        return 0;                       /* "device not present" */
    for (int i = 0; i < n; i++)
        if (!send_byte(b[i], 0))
            return 0;
    return 1;
}

/* Receive one byte from the drive; *eoi set on the last one; -1 on error */
static int recv_byte(int *eoi)
{
    uint8_t b = 0;
    *eoi = 0;
    if (!wait_line(IEC_L_CLK, 0, 5000000))
        return -1;                      /* talker never ready (5 s) */
    c64 &= (uint8_t)~IEC_L_DATA;        /* ready for data */
    if (!wait_line(IEC_L_CLK, 1, 250)) {
        *eoi = 1;                       /* EOI: acknowledge it */
        c64 |= IEC_L_DATA;
        run_us(60);
        c64 &= (uint8_t)~IEC_L_DATA;
        if (!wait_line(IEC_L_CLK, 1, 5000000))
            return -1;
    }
    for (int i = 0; i < 8; i++) {
        if (!wait_line(IEC_L_CLK, 0, 1000))
            return -1;
        if (!bus_low(IEC_L_DATA))
            b |= (uint8_t)(1 << i);
        if (!wait_line(IEC_L_CLK, 1, 1000))
            return -1;
    }
    c64 |= IEC_L_DATA;                  /* frame acknowledge */
    return b;
}

/* TALK 8 on a secondary address, read until EOI, UNTALK */
static int talk_read(uint8_t secondary, uint8_t *out, int max)
{
    const uint8_t talk[] = { 0x48, secondary };
    if (!atn_bytes(talk, 2))
        return bus_fail("TALK") - 1;
    /* Turnaround: C64 becomes listener */
    c64 |= IEC_L_DATA;
    c64 &= (uint8_t)~(IEC_L_ATN | IEC_L_CLK);
    if (!wait_line(IEC_L_CLK, 1, 1000))
        return bus_fail("turnaround") - 1;
    int n = 0, eoi = 0;
    while (!eoi && n < max - 1) {
        int c = recv_byte(&eoi);
        if (c < 0)
            return bus_fail("receive") - 1;
        out[n++] = (uint8_t)c;
    }
    out[n < max ? n : max - 1] = 0;
    run_us(100);
    const uint8_t untalk[] = { 0x5f };
    c64 = 0;
    run_us(100);
    if (!atn_bytes(untalk, 1))
        return bus_fail("UNTALK") - 1;
    c64 = 0;
    run_us(1000);
    return n;
}

/* LISTEN 8, OPEN/CLOSE etc. on a secondary, optional data, UNLISTEN */
static int listen_send(uint8_t secondary, const char *data)
{
    const uint8_t listen[] = { 0x28, secondary };
    if (!atn_bytes(listen, 2))
        return bus_fail("LISTEN");
    c64 &= (uint8_t)~IEC_L_ATN;         /* stay talker: CLK low */
    run_us(100);
    for (int i = 0; data && data[i]; i++)
        if (!send_byte((uint8_t)data[i], data[i + 1] == 0))
            return bus_fail("send");
    const uint8_t unlisten[] = { 0x3f };
    if (!atn_bytes(unlisten, 1))
        return bus_fail("UNLISTEN");
    c64 = 0;
    run_us(100);
    return 1;
}

/* LISTEN 8 on a secondary, n bytes (EOI on the last), UNLISTEN */
static int listen_bytes(uint8_t secondary, const uint8_t *data, int n)
{
    const uint8_t listen[] = { 0x28, secondary };
    if (!atn_bytes(listen, 2))
        return bus_fail("LISTEN");
    c64 &= (uint8_t)~IEC_L_ATN;
    run_us(100);
    for (int i = 0; i < n; i++)
        if (!send_byte(data[i], i == n - 1))
            return bus_fail("send");
    const uint8_t unlisten[] = { 0x3f };
    if (!atn_bytes(unlisten, 1))
        return bus_fail("UNLISTEN");
    c64 = 0;
    run_us(100);
    return 1;
}

/* SAVE"name",8: open channel 1, data, close */
static int save_file(const char *name, const uint8_t *data, int n)
{
    return listen_send(0xf1, name) && listen_bytes(0x61, data, n) && listen_send(0xe1, 0);
}

/* Find a file in a D64 and read it (without the 2-byte link per sector) */
static int d64_read_file(const uint8_t *d64, const char *name, uint8_t *out, int max);

/* LOAD"$",8: print the directory as BASIC LIST would */
static int load_directory(void)
{
    static uint8_t buf[8192];
    if (!listen_send(0xf0, "$"))
        return -1;
    int n = talk_read(0x60, buf, sizeof(buf));
    listen_send(0xe0, 0);
    if (n < 4) {
        fprintf(stderr, "  directory: %d byte(s):", n);
        for (int i = 0; i < n; i++)
            fprintf(stderr, " %02X", buf[i]);
        fprintf(stderr, "\n");
        return -1;
    }
    int lines = 0;
    for (int i = 2; i + 4 <= n; ) {
        if (!buf[i] && !buf[i + 1])
            break;
        printf("    %u ", buf[i + 2] | buf[i + 3] << 8);
        i += 4;
        while (i < n && buf[i]) {
            uint8_t ch = buf[i++];
            putchar(ch >= 0x20 && ch < 0x7f ? ch : (ch == 0x12 ? ' ' : '.'));
        }
        putchar('\n');
        i++;
        lines++;
    }
    return lines;
}

/* Byte offset of a sector in a D64 */
static uint32_t d64_offset(unsigned t, unsigned s)
{
    uint32_t o = 0;
    for (unsigned i = 1; i < t; i++)
        o += i <= 17 ? 21 : i <= 24 ? 19 : i <= 30 ? 18 : 17;
    return (o + s) * 256;
}

static int d64_read_file(const uint8_t *d64, const char *name, uint8_t *out, int max)
{
    unsigned t = 18, s = 1;
    size_t len = strlen(name);
    while (t) {
        const uint8_t *sec = d64 + d64_offset(t, s);
        for (int e = 0; e < 8; e++) {
            const uint8_t *ent = sec + e * 32;
            if ((ent[2] & 0x07) && !memcmp(ent + 5, name, len) &&
                (len == 16 || ent[5 + len] == 0xa0)) {
                int n = 0;
                unsigned ft = ent[3], fs = ent[4];
                while (ft && ft <= 40 && n < max - 254) {
                    const uint8_t *fsec = d64 + d64_offset(ft, fs);
                    int used = fsec[0] ? 254 : fsec[1] - 1;
                    memcpy(out + n, fsec + 2, (size_t)used);
                    n += used;
                    ft = fsec[0];
                    fs = fsec[1];
                }
                return n;
            }
        }
        t = sec[0];
        s = sec[1];
    }
    return -1;
}

int main(int argc, char **argv)
{
    static uint8_t rom[16384];

    if (argc < 2) {
        fprintf(stderr, "usage: %s ROMFILE\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
        fprintf(stderr, "cannot read 16 KB from %s\n", argv[1]);
        return 2;
    }
    fclose(f);

    drive1541_init(&drive, rom, 8);
    drive1541_reset(&drive);
    drive1541_run(&drive, 2000000);

    int fails = 0;
    const char *msg = "CBM DOS V2.6 1541";
    void *hit = memmem(drive.ram, sizeof(drive.ram), msg, strlen(msg));
    printf("power-on message in RAM: %s\n", hit ? "yes" : "NO");
    fails += !hit;

    printf("PC $%04X, cycles %llu (platform hook %llu), jammed %d\n",
           drive.cpu.pc, (unsigned long long)drive.cpu.cycles,
           (unsigned long long)platform_cycles, drive.cpu.jammed);
    fails += drive.cpu.jammed || platform_cycles != drive.cpu.cycles;

    printf("IEC out: %s%s\n", (drive.iec_out & IEC_L_CLK) ? "CLK low " : "",
           (drive.iec_out & IEC_L_DATA) ? "DATA low" : "");
    printf("VIA1 DDRB %02X ORB %02X IER %02X PCR %02X; VIA2 DDRB %02X ORB %02X (LED %s, motor %s)\n",
           drive.via1.ddrb, drive.via1.orb, drive.via1.ier, drive.via1.pcr,
           drive.via2.ddrb, drive.via2.orb,
           (drive.via2.orb & 0x08) ? "on" : "off", (drive.via2.orb & 0x04) ? "on" : "off");
    fails += drive.iec_out != 0;

    char buf[64];
    int n = talk_read(0x6f, (uint8_t *)buf, sizeof(buf));
    for (int i = 0; i < n; i++)
        if (buf[i] == 13) buf[i] = 0;
    printf("error channel over IEC: \"%s\"\n", n > 0 ? buf : "(failed)");
    fails += n <= 0 || strcmp(buf, "73,CBM DOS V2.6 1541,00,00") != 0;
    printf("bus released afterwards: %s\n", drive.iec_out ? "NO" : "yes");
    fails += drive.iec_out != 0;

    if (argc > 2) {
        static uint8_t d64[200000];
        static struct disk1541 disk;
        f = fopen(argv[2], "rb");
        uint32_t size = f ? (uint32_t)fread(d64, 1, sizeof(d64), f) : 0;
        if (f)
            fclose(f);
        if (disk_from_d64(&disk, d64, size)) {
            printf("%s: not a D64\n", argv[2]);
            return 2;
        }
        drive1541_insert(&drive, &disk);
        uint64_t start = drive.cpu.cycles;
        int lines = load_directory();
        printf("LOAD\"$\",8: %d lines in %.2f s drive time\n", lines,
               (drive.cpu.cycles - start) / 1e6);
        fails += lines <= 0;
        /* LOAD"*",8: the first file, compared with the D64 itself */
        static uint8_t want[65536], got[65536];
        uint32_t wn = 0;
        const uint8_t *dir = d64 + d64_offset(18, 1);
        unsigned t = dir[3], s = dir[4];        /* first entry: track, sector */
        while (t && t <= 40 && wn < sizeof(want) - 256) {
            const uint8_t *sec = d64 + d64_offset(t, s);
            unsigned used = sec[0] ? 254 : sec[1] - 1u;
            memcpy(want + wn, sec + 2, used);
            wn += used;
            t = sec[0];
            s = sec[1];
        }
        start = drive.cpu.cycles;
        if (!listen_send(0xf0, "*"))
            n = -1;
        else
            n = talk_read(0x60, got, sizeof(got));
        listen_send(0xe0, 0);
        int same = n == (int)wn && !memcmp(got, want, wn);
        printf("LOAD\"*\",8: %d bytes in %.2f s drive time (%.0f bytes/s), %s the D64\n",
               n, (drive.cpu.cycles - start) / 1e6, n / ((drive.cpu.cycles - start) / 1e6),
               same ? "identical to" : "DIFFERENT from");
        fails += !same;

        n = talk_read(0x6f, (uint8_t *)buf, sizeof(buf));
        for (int i = 0; i < n; i++)
            if (buf[i] == 13) buf[i] = 0;
        printf("error channel: \"%s\"\n", n > 0 ? buf : "(failed)");
        fails += n <= 0 || strncmp(buf, "00,", 3) != 0;

        /* SAVE a file, then decode the written tracks back into the D64 */
        static uint8_t data[3000], back[65536];
        static uint8_t changed[768];
        for (int i = 0; i < (int)sizeof(data); i++)
            data[i] = (uint8_t)(i * 7 + (i >> 8));
        start = drive.cpu.cycles;
        int saved = save_file("PISD2IEC TEST", data, sizeof(data));
        run_us(3000000);                /* let the DOS finish writing */
        n = talk_read(0x6f, (uint8_t *)buf, sizeof(buf));
        for (int i = 0; i < n; i++)
            if (buf[i] == 13) buf[i] = 0;
        printf("SAVE: %s in %.2f s drive time, error channel \"%s\"\n",
               saved ? "sent" : "FAILED", (drive.cpu.cycles - start) / 1e6, n > 0 ? buf : "?");
        int nchanged = disk_to_d64(&disk, d64, size, changed);
        int nback = d64_read_file(d64, "PISD2IEC TEST", back, sizeof(back));
        int ok = nback == (int)sizeof(data) && !memcmp(back, data, sizeof(data));
        printf("written back to the D64: %d sectors changed, file %s (%d bytes)\n",
               nchanged, ok ? "identical" : "WRONG", nback);
        if (!strncmp(buf, "72,", 3))
            printf("(disk full: SAVE test skipped)\n");
        else
            fails += !saved || !ok || strncmp(buf, "00,", 3) != 0;
    }

    if (argc > 2) {
        /*
         * Reset after the SAVE: the DOS must start over and still read
         * the disk, and the head must end on a whole track.
         */
        unsigned steps0 = drive.steps;
        drive1541_reset(&drive);
        drive1541_run(&drive, 2000000);
        int lines = load_directory();
        printf("after a reset: LOAD\"$\" %d lines, %u steps, head on track %u%s\n",
               lines, drive.steps - steps0, drive.ht / 2 + 1, (drive.ht & 1) ? ".5" : "");
        fails += lines <= 0 || (drive.ht & 1);
    }

    printf("1541: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
