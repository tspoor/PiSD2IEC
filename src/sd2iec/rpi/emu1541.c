// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// emu1541.c - the cycle-exact 1541 (src/emu/) on the Pi, in core 1
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// sd2iec calls display_service() on core 1 while its bus loop is idle; the
// browser (rpi/browser.c) calls emu1541_run_d64() from there when a D64 is
// selected. From then on core 1 is the 1541: sd2iec stays frozen in its
// idle loop until the emulation stops (Exit), and then simply continues.
//
// Real time: drive_platform_wait() runs at the start of every emulated
// clock cycle. It busy-waits until that cycle's moment on the ARM counter
// (CNTPCT, 19.2 MHz: 19.2 ticks per 1 MHz cycle, kept exact by adding 19
// and every fifth cycle 20) and samples ATN/CLK/DATA. Right after the
// CPU's access, drive_platform_output() drives the IEC lines, so a write
// to VIA1 reaches the bus in the same cycle, as on a real 1541.
//
// The 1541's automatic ATN acknowledge (DATA = PB1 OR (ATN in XOR ATNA))
// is a gate, not clocked: DATA follows ATN within nanoseconds. Fast loaders
// use it to send bits (ATNA = bit, the C64 toggles ATN and reads DATA right
// away). So while waiting for the next cycle, the Pi keeps re-evaluating
// that gate from the live ATN input, as the hardware would. If emulating a cycle ever
// took longer than 1 us, the next cycles run without waiting until the
// drive is back on time ('late' counts those cycles).
//
// During the emulation:
//   - core 3 must not answer ATN for sd2iec (rpi_atn_irq = 0);
//   - the HAT LED shows the emulated drive LED (rpi_emu_active).

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "dirent.h"
#include "fatops.h"
#include "ff.h"
#include "parser.h"
#include "hal/timer.h"
#include "emu/drive1541.h"
#include "emu/disk1541.h"
#include "storage.h"
#include "emu1541.h"
#include "petscii.h"

#define CHUNK_CYCLES 10000u             /* status/stop check every 10 ms */

volatile uint8_t rpi_emu_active;

static uint8_t rom[16384];
static int     rom_loaded;
static uint8_t d64[197376];
static struct disk1541  disk;
static struct drive1541 drive;
static struct drive1541 snapshot;       /* copy at the watch address */
static uint16_t watch_pc;               /* 0 = none */

static volatile uint8_t stop_request;
static volatile uint32_t swap_request;  /* list index + 1, 0 = none */
static uint32_t swaps, swap_failed;     /* for 'd' */
static uint8_t  reset_low;              /* IEC RESET seen low in the last sample */

/* Writing back: the D64 file of the disk in the drive, and its sectors */
static uint8_t  d64_name[sizeof(((cbmdirent_t *)0)->pvt.fat.realname)];
static uint32_t d64_size;
static uint8_t  changed[768];
static uint32_t flushed, flush_failed;  /* sectors, for 'd' */
static uint64_t motor_off_since;        /* drive cycle */
static uint32_t resets;                 /* for 'd' */
static path_t emu_path;                 /* directory of the D64 files */
static struct emu1541_status status;

/* Real time */
static uint64_t next_tick;
static unsigned fifth;
static uint8_t  out_now;
static uint32_t late;
static uint64_t late_max;               /* in counter ticks */
static uint64_t cycle_start;            /* when the current cycle began */
static uint64_t busy_sum, busy_cycles;  /* time spent per cycle, in ticks */
static uint32_t busy_max;

/* The last late cycles: when, how late, where (for 'd') */
#define LATE_LOG 16
static struct { uint64_t cycle; uint32_t ticks; uint16_t pc; } late_log[LATE_LOG];
static uint32_t late_log_n;

void emu1541_get_status(struct emu1541_status *out)
{
    memcpy(out, (const void *)&status, sizeof(*out));
}

void emu1541_request_stop(void)
{
    stop_request = 1;
}

void emu1541_request_swap(uint32_t index)
{
    swap_request = index + 1;
}

/* Register log at chosen addresses (console 'l' and 'p') */
static struct drive_log log_buf[DRIVE_LOG];
static uint16_t log_pcs[4];
static uint8_t  log_count;

void emu1541_set_log(const uint16_t *pcs, unsigned n)
{
    if (n > 4)
        n = 4;
    drive.log_pcs = 0;                  /* off while changing */
    for (unsigned i = 0; i < n; i++)
        log_pcs[i] = drive.log_pc[i] = pcs[i];
    log_count = (uint8_t)n;
    drive.log = log_buf;
    drive.log_n = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    drive.log_pcs = (uint8_t)n;
}

void emu1541_print_log(void (*print)(const char *fmt, ...))
{
    uint32_t total = drive.log_n;
    uint32_t n = total < DRIVE_LOG ? total : DRIVE_LOG;
    print("1541 log: %u entries (showing %u): cycle PC A X Y P\n", total, n);
    for (uint32_t k = 0; k < n; k++) {
        const struct drive_log *l = &log_buf[(total - n + k) % DRIVE_LOG];
        print("%llu %04X %02X %02X %02X %02X\n", (unsigned long long)l->cycle,
              l->pc, l->a, l->x, l->y, l->p);
    }
}

void emu1541_set_watch(uint16_t pc)
{
    watch_pc = pc;
    drive.watch_hit = 0;
    drive.watch_pc = pc;
}

static void dump_drive(void (*print)(const char *fmt, ...), const struct drive1541 *d);

/* A racy look at the live drive from another core: good enough to debug */
void emu1541_dump(void (*print)(const char *fmt, ...))
{
    if (!status.active) {
        print("1541 emulation is not running\n");
        return;
    }
    if (watch_pc && drive.watch_hit) {
        print("=== snapshot: first time at $%04X ===\n", watch_pc);
        dump_drive(print, &snapshot);
        print("=== now ===\n");
    } else if (watch_pc) {
        print("(watch $%04X not reached yet)\n", watch_pc);
    }
    dump_drive(print, &drive);
}

static void dump_drive(void (*print)(const char *fmt, ...), const struct drive1541 *d)
{
    print("1541: PC $%04X A %02X X %02X Y %02X S %02X P %02X, cycle %llu, late %u (max %u ns)\n",
          d->cpu.pc, d->cpu.a, d->cpu.x, d->cpu.y, d->cpu.s, d->cpu.p,
          (unsigned long long)d->cpu.cycles, late, status.late_max_ns);
    if (d == &drive)
        print("ROM: %s; disk: %s (\"%s\", ID %02X %02X in BAM); swaps %u, failed %u; DOS sees ID %02X %02X; resets %u; saved %u sectors (%u failed)%s\n",
              status.rom[0] ? status.rom : "?", d->disk ? "in" : "NONE", status.name,
              d64[357 * 256 + 0xa2], d64[357 * 256 + 0xa3], swaps, swap_failed,
              d->ram[0x12], d->ram[0x13], resets, flushed, flush_failed,
              disk.written ? ", unsaved writes" : "");
    if (d == &drive && busy_cycles)
        print("work per cycle: average %u ns, max %u ns (of 1000)\n",
              (unsigned)(busy_sum * 10000u / 192u / busy_cycles),
              (unsigned)((uint64_t)busy_max * 10000u / 192u));
    print("head: track %u%s, motor %s, LED %s, zone %u; syncs %u, bytes %u, steps %u\n",
          d->ht / 2 + 1, (d->ht & 1) ? ".5" : "",
          (d->via2.orb & d->via2.ddrb & 4) ? "on" : "off",
          (d->via2.orb & d->via2.ddrb & 8) ? "on" : "off",
          (d->via2.orb >> 5) & 3, d->syncs, d->bytes, d->steps);
    for (int n = 0; n < 2; n++) {
        const struct via6522 *v = n ? &d->via2 : &d->via1;
        print("VIA%d: ORA %02X DDRA %02X ORB %02X DDRB %02X PCR %02X ACR %02X IFR %02X IER %02X T1 %04X/%04X T2 %04X\n",
              n + 1, v->ora, v->ddra, v->orb, v->ddrb, v->pcr, v->acr, v->ifr, v->ier,
              v->t1c, v->t1l, v->t2c);
    }
    print("IEC: in %s%s%s, out %s%s\n",
          (d->iec_in & IEC_L_ATN) ? "ATN " : "", (d->iec_in & IEC_L_CLK) ? "CLK " : "",
          (d->iec_in & IEC_L_DATA) ? "DATA " : "",
          (d->iec_out & IEC_L_CLK) ? "CLK " : "", (d->iec_out & IEC_L_DATA) ? "DATA" : "");
    if (d == &drive && late_log_n) {
        uint32_t n = late_log_n < LATE_LOG ? late_log_n : LATE_LOG;
        print("late cycles (last %u of %u stretches): cycle / ns / PC\n", n, late_log_n);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t i = (late_log_n - n + k) % LATE_LOG;
            print("  %llu  %u ns  $%04X\n", (unsigned long long)late_log[i].cycle,
                  (unsigned)((uint64_t)late_log[i].ticks * 10000u / 192u), late_log[i].pc);
        }
    }
    print("last instructions:");
    for (int i = 0; i < 32; i++)
        print(" %04X", d->pc_hist[(d->pc_hist_pos + i) & 31]);
    print("\nRAM:");
    for (int i = 0; i < 0x800; i++) {
        if (!(i & 31))
            print("\n %04X:", i);
        print(" %02X", d->ram[i]);
    }
    print("\n");
}

/* ----- the platform hook of drive1541.c ----- */

/* The ATN acknowledge gate, from the live ATN level (see above) */
static inline void atn_gate(struct drive1541 *d, uint32_t lev)
{
    uint8_t pb = d->via1.orb & d->via1.ddrb;
    int atn_low  = !((lev >> HAT_IN_ATN) & 1);
    int data_low = (pb & 0x02) || (atn_low ^ ((pb >> 4) & 1));
    if (data_low != !!(out_now & IEC_L_DATA)) {
        out_now ^= IEC_L_DATA;
        set_data(!data_low);
    }
}

/*
 * After a write to VIA1 (and at the end of every cycle): the lines as the
 * drive computed them, without reading the GPIOs first, so the change is on
 * the pins as early as possible. An ATN edge after the last sample is
 * caught by the gate in the wait loop within the same microsecond.
 * 7406: GPIO high = line low.
 */
void drive_platform_output(struct drive1541 *d)
{
    uint8_t diff = d->iec_out ^ out_now;
    if (!diff)
        return;
    out_now = d->iec_out;
    if (diff & IEC_L_CLK)
        set_clock(!(out_now & IEC_L_CLK));
    if (diff & IEC_L_DATA)
        set_data(!(out_now & IEC_L_DATA));
}

void drive_platform_wait(struct drive1541 *d)
{
    /* Wait for this cycle, keeping the ATN gate up to date meanwhile */
    next_tick += 19;
    if (++fifth == 5) {
        fifth = 0;
        next_tick++;
    }
    uint64_t now = timer_ticks();

    /* How long the last cycle took to emulate (for 'd') */
    uint32_t busy = (uint32_t)(now - cycle_start);
    busy_sum += busy;
    busy_cycles++;
    if (busy > busy_max && busy_cycles > 1000)
        busy_max = busy;

    if (now > next_tick) {
        uint64_t by = now - next_tick;
        late++;
        if (by > late_max)
            late_max = by;
        /* Log the start of each late stretch (not every cycle of it) */
        if (by > 2) {
            uint32_t i = late_log_n % LATE_LOG;
            uint32_t p = (late_log_n + LATE_LOG - 1) % LATE_LOG;
            if (!late_log_n || d->cpu.cycles > late_log[p].cycle + 50) {
                late_log[i].cycle = d->cpu.cycles;
                late_log[i].ticks = (uint32_t)by;
                late_log[i].pc    = d->cpu.pc;
                late_log_n++;
            }
        }
    }
    /* Until shortly before the moment: also follow ATN with the gate (a
       round costs a GPIO read); the last ~0.3 us only the counter, so the
       cycle starts as close to its moment as possible */
    while (now + 6 < next_tick) {
        atn_gate(d, GPLEV0);
        now = timer_ticks();
    }
    while (now < next_tick)
        now = timer_ticks();
    cycle_start = now;
}

void drive_platform_sample(struct drive1541 *d)
{
    /* Inputs not inverted: GPIO high = released */
    uint32_t lev = GPLEV0;
    if (!((lev >> HAT_IN_RESET) & 1))
        reset_low = 1;
    d->iec_in = (uint8_t)(((lev >> HAT_IN_ATN)   & 1 ? 0 : IEC_L_ATN) |
                          ((lev >> HAT_IN_CLOCK) & 1 ? 0 : IEC_L_CLK) |
                          ((lev >> HAT_IN_DATA)  & 1 ? 0 : IEC_L_DATA));
}

/* ----- loading ----- */

static int read_file(uint8_t part, uint32_t dir, const char *name,
                     uint8_t *buf, uint32_t max, uint32_t *size)
{
    FIL fil;
    UINT got;

    partition[part].fatfs.curr_dir = dir;
    if (f_open(&partition[part].fatfs, &fil, (const UCHAR *)name, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return -1;
    /* FatFs reads at most 65535 bytes at a time (UINT is 16 bits here) */
    uint32_t total = 0;
    FRESULT res = FR_OK;
    while (total < max) {
        uint32_t want = max - total > 32768u ? 32768u : max - total;
        res = f_read(&fil, buf + total, (UINT)want, &got);
        if (res != FR_OK)
            break;
        total += got;
        if (got < want)
            break;                      /* end of file */
    }
    f_close(&fil);
    if (res != FR_OK)
        return -1;
    *size = total;
    return 0;
}

/*
 * The drive ROM. By default DOS1541 (or DOS1541.BIN, 1541.ROM, 1541.BIN)
 * from the root of the SD card. Another ROM can be chosen in the browser
 * (Add on the file, see emu1541_set_rom); the choice is kept in
 * PISD2IEC.CFG in the root as "ROM <directory cluster> <8.3 name>".
 * A 16 KB ROM is used as it is. An 8 KB ROM is only the upper chip
 * ($E000-$FFFF, as JiffyDOS often comes): the lower half comes from the
 * default ROM, as in a 1541 where only that chip is replaced.
 */
#define CONFIG_FILE "PISD2IEC.CFG"

static uint8_t  rom_file[16384 + 1];
static char     rom_choice[13];         /* 8.3 name, "" = default */
static uint32_t rom_choice_dir;
static int      config_read;

static int load_default_rom(void)
{
    static const char *const names[] = { "DOS1541", "DOS1541.BIN", "1541.ROM", "1541.BIN" };
    uint32_t size;

    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!read_file(0, 0, names[i], rom, sizeof(rom), &size) && size == sizeof(rom)) {
            strcpy(status.rom, "dos1541");
            return 0;
        }
    return -1;
}

static void read_config(void)
{
    uint32_t size;
    char buf[64];

    config_read = 1;
    if (read_file(0, 0, CONFIG_FILE, (uint8_t *)buf, sizeof(buf) - 1, &size))
        return;
    buf[size] = 0;
    if (strncmp(buf, "ROM ", 4))
        return;
    char *p = buf + 4;
    uint32_t dir = 0;
    while (*p >= '0' && *p <= '9')
        dir = dir * 10 + (uint32_t)(*p++ - '0');
    if (*p++ != ' ')
        return;
    unsigned n = 0;
    while (*p > ' ' && n < sizeof(rom_choice) - 1)
        rom_choice[n++] = *p++;
    rom_choice[n] = 0;
    rom_choice_dir = dir;
}

static int write_config(void)
{
    char buf[64], *p = buf;
    FIL fil;
    UINT w;

    memcpy(p, "ROM ", 4);
    p += 4;
    char num[12];
    unsigned k = 0;
    uint32_t v = rom_choice_dir;
    do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k)
        *p++ = num[--k];
    *p++ = ' ';
    for (const char *s = rom_choice; *s; s++)
        *p++ = *s;
    *p++ = '\n';

    partition[0].fatfs.curr_dir = 0;
    if (f_open(&partition[0].fatfs, &fil, (const UCHAR *)CONFIG_FILE,
               FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return -1;
    FRESULT res = f_write(&fil, buf, (UINT)(p - buf), &w);
    f_close(&fil);
    return res == FR_OK ? 0 : -1;
}

/* Lowercase 8.3 name for the OLED */
static void show_rom_name(const char *name)
{
    unsigned i;
    for (i = 0; name[i] && i < sizeof(status.rom) - 1; i++)
        status.rom[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    status.rom[i] = 0;
}

/* Read a chosen ROM file into rom[]; 0 on success */
static int load_rom_file(uint32_t dir, const char *name)
{
    uint32_t size;

    if (read_file(0, dir, name, rom_file, sizeof(rom_file), &size))
        return -1;
    if (size == 16384) {
        memcpy(rom, rom_file, 16384);
    } else if (size == 8192) {
        if (load_default_rom())
            return -1;
        memcpy(rom + 8192, rom_file, 8192);
    } else {
        return -1;
    }
    show_rom_name(name);
    return 0;
}

static int load_rom(void)
{
    if (rom_loaded)
        return 0;
    if (!config_read)
        read_config();
    if (rom_choice[0] && !load_rom_file(rom_choice_dir, rom_choice)) {
        rom_loaded = 1;
        return 0;
    }
    if (load_default_rom())
        return -1;
    rom_loaded = 1;
    return 0;
}

int emu1541_set_rom(path_t *path, cbmdirent_t *dent, char *note)
{
    const char *name = (const char *)dent->pvt.fat.realname;

    if (partition[path->part].fop != &fatops) {
        strcpy(note, "Not on SD card");
        return -1;
    }
    if (load_rom_file(path->dir.fat, name)) {
        strcpy(note, "Not 8/16 KB ROM");
        rom_loaded = 0;
        return -1;
    }
    strncpy(rom_choice, name, sizeof(rom_choice) - 1);
    rom_choice_dir = path->dir.fat;
    config_read = 1;
    rom_loaded = 1;
    if (write_config()) {
        strcpy(note, "Cannot write CFG");
        return 1;                       /* in use now, but not kept */
    }
    return 0;
}

/* ----- run ----- */

static void publish(void)
{
    status.track     = (uint8_t)(drive.ht / 2 + 1);
    status.halftrack = drive.ht & 1;
    status.motor     = (drive.via2.orb & drive.via2.ddrb & 0x04) != 0;
    status.led       = (drive.via2.orb & drive.via2.ddrb & 0x08) != 0;
    status.late      = late;
    uint64_t ns = late_max * 10000u / 192u;                        /* 19.2 ticks per us */
    status.late_max_ns = ns > 0xffffffffu ? 0xffffffffu : (uint32_t)ns;
    status.seconds   = (uint32_t)(drive.cpu.cycles / 1000000u);
}

/* Directory name to the status, as the browser shows it */
static void set_name(const cbmdirent_t *dent)
{
    rpi_copy_pet(status.name, BROWSE_NAME_LEN, dent->name, CBM_NAME_LENGTH, 1);
}

static void run_chunk(void)
{
    drive1541_run(&drive, CHUNK_CYCLES);
    if ((drive.via2.orb & drive.via2.ddrb & 0x08))
        GPSET0 = 1u << HAT_LED;
    else
        GPCLR0 = 1u << HAT_LED;
    publish();
}

/* Remember which file the disk in the drive came from */
static void set_file(const cbmdirent_t *dent, uint32_t size)
{
    memcpy(d64_name, dent->pvt.fat.realname, sizeof(d64_name));
    d64_size = size;
}

/*
 * Core 0 does its SD card work in small portions with short pauses in
 * between: the SD controller sits on the same peripheral bus as the GPIOs,
 * and core 0's memory traffic shares the L2 cache with core 1. In one go,
 * that would delay some of the drive's cycles by microseconds.
 */
#define PACE_US     200u
#define PACE_PIECE  4096u               /* bytes read from the card at a time */

static uint8_t pacing;                  /* set while core 0 runs a job */

static void pace(void)
{
    if (pacing)
        delay_us(PACE_US);
}

/* read_file, in small pieces with pauses (core 0, during the emulation) */
static int read_file_paced(uint8_t part, uint32_t dir, const char *name,
                           uint8_t *buf, uint32_t max, uint32_t *size)
{
    FIL fil;
    UINT got;
    uint32_t total = 0;
    FRESULT res = FR_OK;

    partition[part].fatfs.curr_dir = dir;
    if (f_open(&partition[part].fatfs, &fil, (const UCHAR *)name, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return -1;
    while (total < max) {
        uint32_t want = max - total > PACE_PIECE ? PACE_PIECE : max - total;
        res = f_read(&fil, buf + total, (UINT)want, &got);
        if (res != FR_OK)
            break;
        total += got;
        if (got < want)
            break;
        pace();
    }
    f_close(&fil);
    if (res != FR_OK)
        return -1;
    *size = total;
    return 0;
}

/*
 * Bring what the drive wrote back into the D64 file: decode the given
 * tracks and write only the sectors that changed, 256 bytes each. Runs on
 * whichever core owns the SD card at that moment (see the jobs below).
 */
static void write_back(const uint8_t *tracks)
{
    memset(changed, 0, sizeof(changed));
    int n = disk_tracks_to_d64(&disk, tracks, d64, d64_size, changed);
    if (n <= 0)
        return;
    FIL fil;
    UINT w;
    partition[emu_path.part].fatfs.curr_dir = emu_path.dir.fat;
    if (f_open(&partition[emu_path.part].fatfs, &fil, d64_name,
               FA_WRITE | FA_OPEN_EXISTING) != FR_OK) {
        flush_failed += (uint32_t)n;
        return;
    }
    for (unsigned i = 0; i < sizeof(changed); i++) {
        if (!changed[i])
            continue;
        if (f_lseek(&fil, i * 256u) == FR_OK &&
            f_write(&fil, d64 + i * 256u, 256, &w) == FR_OK && w == 256)
            flushed++;
        else
            flush_failed++;
        pace();
    }
    f_close(&fil);
}

/*
 * SD card work during the emulation. A real 1541 never stands still, and
 * some fast loaders clock their bytes blind (no handshake): if the drive
 * paused for the tens of milliseconds that reading a D64 or saving takes,
 * such a transfer would break. So core 1 hands that work to core 0 and
 * keeps the drive running meanwhile; core 0 picks it up in its main loop
 * (emu1541_worker). While a job runs, only core 0 touches the SD card,
 * FatFs and d64[]; the drive only reads/writes the GCR tracks of 'disk'.
 */
enum { JOB_IDLE, JOB_FLUSH, JOB_LOAD };
static volatile uint8_t job;            /* core 1 sets it, core 0 clears it when done */
static uint8_t  job_tracks[DISK_HALFTRACKS / 2];  /* JOB_FLUSH: tracks to save */
static cbmdirent_t job_dent;            /* JOB_LOAD: the D64 to read */
static uint32_t job_size;               /* JOB_LOAD: its size */
static int      job_ok;
static uint8_t  saving_async;           /* a JOB_FLUSH is running */

/* Core 0 */
void emu1541_worker(void)
{
    uint8_t j = __atomic_load_n(&job, __ATOMIC_ACQUIRE);

    if (j != JOB_FLUSH && j != JOB_LOAD)
        return;
    pacing = 1;
    disk_pace = pace;
    if (j == JOB_FLUSH) {
        write_back(job_tracks);
    } else {
        /* The drive is empty meanwhile, so 'disk' is free to rebuild */
        job_ok = !read_file_paced(emu_path.part, emu_path.dir.fat,
                                  (const char *)job_dent.pvt.fat.realname,
                                  d64, sizeof(d64), &job_size) &&
                 !disk_from_d64(&disk, d64, job_size);
    }
    disk_pace = 0;
    pacing = 0;
    __atomic_store_n(&job, JOB_IDLE, __ATOMIC_RELEASE);
}

static int job_busy(void)
{
    return __atomic_load_n(&job, __ATOMIC_ACQUIRE) != JOB_IDLE;
}

static void job_start(uint8_t j)
{
    __atomic_store_n(&job, j, __ATOMIC_RELEASE);
}

/* Core 1: hand what was written so far to core 0 (the drive keeps running;
   what it writes from now on marks its tracks dirty again) */
static void start_flush(void)
{
    memcpy(job_tracks, disk.dirty, sizeof(job_tracks));
    memset(disk.dirty, 0, sizeof(disk.dirty));
    disk.written = 0;
    status.saving = 1;
    saving_async = 1;
    job_start(JOB_FLUSH);
}

/* Core 1, at the end of the emulation: save the rest right here */
static void flush_disk(void)
{
    while (job_busy())
        ;                               /* core 0 finishes its job first */
    if (!disk.written)
        return;
    status.saving = 1;
    memcpy(job_tracks, disk.dirty, sizeof(job_tracks));
    memset(disk.dirty, 0, sizeof(disk.dirty));
    disk.written = 0;
    write_back(job_tracks);
    status.saving = 0;
}

/*
 * IEC RESET low: like a real 1541, the drive is held in reset (the VIAs
 * let go of the bus) until RESET goes high again, then it starts over:
 * the DOS runs its power-on code. The disk stays in.
 */
static void hold_reset(void)
{
    resets++;
    drive.iec_out = 0;
    drive_platform_output(&drive);
    GPCLR0 = 1u << HAT_LED;
    while (!stop_request && !((GPLEV0 >> HAT_IN_RESET) & 1))
        ;
    reset_low = 0;
    /* Continue from now; set before the reset, because the 6502's reset
       sequence already makes bus cycles (else the whole time in reset
       would count as one late cycle) */
    next_tick = timer_ticks();
    cycle_start = next_tick;
    fifth = 0;
    drive1541_reset(&drive);
}

/*
 * After the drive was deliberately held for a moment between two chunks
 * (taking a disk out or putting one in: rarely run code, so a few us):
 * continue from now, like after a reset, instead of rushing to catch up.
 * Those few us are not the emulation being slow, so they are not 'late'.
 */
static void resync(void)
{
    next_tick = timer_ticks();
    cycle_start = next_tick;
    fifth = 0;
}

/* One step of the drive: 10 ms, and a bus reset if one came */
static void emu_step(void)
{
    run_chunk();
    if (reset_low)
        hold_reset();
    if (saving_async && !job_busy()) {
        saving_async = 0;
        status.saving = 0;
    }
}

/* Keep the drive running until core 0 has finished its job */
static void run_until_job_done(void)
{
    while (job_busy() && !stop_request)
        emu_step();
}

/*
 * Change the disk like a person would: take the old one out (for half a
 * second the drive has no disk: no data, and the write protect sensor sees
 * light, which is how the DOS and loaders notice a change), then put the
 * new one in. Saving the old disk and reading the new D64 happen on core 0
 * (see the jobs above), so the drive keeps running all the time.
 */
static void swap_disk(uint32_t index)
{
    cbmdirent_t *dent = browse_d64_dent(index);

    if (!dent) {
        swap_failed++;
        return;
    }
    swaps++;
    status.swapping = 1;

    /* Whatever was written to the old disk goes to its D64 first */
    run_until_job_done();
    if (disk.written && !stop_request) {
        start_flush();
        run_until_job_done();
    }

    drive1541_insert(&drive, 0);
    job_dent = *dent;
    job_start(JOB_LOAD);
    resync();
    uint64_t until = drive.cpu.cycles + 500000;     /* 0.5 s without a disk */
    while ((job_busy() || drive.cpu.cycles < until) && !stop_request)
        emu_step();
    while (job_busy())
        ;                               /* stopping: let core 0 finish */

    if (job_ok) {
        set_name(&job_dent);
        set_file(&job_dent, job_size);
        drive1541_insert(&drive, &disk);
    } else {
        swap_failed++;                  /* the drive stays empty */
    }
    status.swapping = 0;
    resync();
}

int emu1541_run_d64(path_t *path, cbmdirent_t *dent, char *note, void (*started)(void))
{
    uint32_t size;

    if (load_rom()) {
        strcpy(note, "No DOS1541 on SD");
        return -1;
    }
    if (read_file(path->part, path->dir.fat, (const char *)dent->pvt.fat.realname,
                  d64, sizeof(d64), &size)) {
        strcpy(note, "Cannot read D64");
        return -1;
    }
    if (disk_from_d64(&disk, d64, size)) {
        strcpy(note, "Not a D64 image");
        return -1;
    }

    char rom_name[sizeof(status.rom)];
    memcpy(rom_name, status.rom, sizeof(rom_name));
    memset(&status, 0, sizeof(status));
    memcpy(status.rom, rom_name, sizeof(rom_name));
    set_name(dent);
    set_file(dent, size);
    flushed = flush_failed = 0;
    emu_path = *path;
    swap_request = 0;
    swaps = swap_failed = 0;

    /* Take over: sd2iec's ATN answer off, bus released, LED ours */
    uint8_t atn_irq = rpi_atn_irq;
    rpi_atn_irq = 0;
    set_clock(1);
    set_data(1);
    out_now = 0;
    rpi_emu_active = 1;
    stop_request = 0;
    late = 0;
    late_max = 0;
    late_log_n = 0;
    busy_sum = busy_cycles = 0;
    busy_max = 0;
    saving_async = 0;
    status.active = 1;
    started();

    drive1541_init(&drive, rom, 8);
    drive1541_insert(&drive, &disk);
    drive.watch = &snapshot;
    drive.watch_pc = watch_pc;
    drive.log = log_buf;
    for (unsigned i = 0; i < log_count; i++)
        drive.log_pc[i] = log_pcs[i];
    drive.log_pcs = log_count;
    next_tick = timer_ticks();
    cycle_start = next_tick;
    fifth = 0;
    drive1541_reset(&drive);
    resync();                           /* cold start: the reset cycles do not count */
    late = 0;
    late_max = 0;
    late_log_n = 0;

    reset_low = 0;
    resets = 0;
    while (!stop_request) {
        emu_step();

        /* Written to, and the motor has been off for 2 s: save (core 0) */
        if (disk.written) {
            if (drive.via2.orb & drive.via2.ddrb & 0x04)
                motor_off_since = drive.cpu.cycles;
            else if (drive.cpu.cycles - motor_off_since > 2000000 && !job_busy())
                start_flush();
        }
        uint32_t swap = swap_request;
        if (swap) {
            swap_request = 0;
            swap_disk(swap - 1);
        }
    }

    flush_disk();

    /* Hand the bus back to sd2iec */
    set_clock(1);
    set_data(1);
    GPCLR0 = 1u << HAT_LED;
    status.active = 0;
    rpi_emu_active = 0;
    rpi_reset_seen = 0;                 /* resets were for the 1541, not sd2iec */
    rpi_atn_irq = atn_irq;
    return 0;
}

/*
 * The drive itself, compiled into this file: then the compiler can inline
 * the platform hooks above into the per-cycle code (src/emu/drive1541.c is
 * not built separately for the Pi; the PC tests build it on its own).
 */
#include "emu/drive1541.c"
