// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// sdcard.c - SD card through the BCM2710's EMMC controller (Arasan SDHCI)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Replaces sd2iec's src/sdcard.c (SPI) with the same API (sdcard.h). As
// there, the functions are aliased as disk_*, so diskio/FatFs use them
// directly.
//
// The firmware connects the SD card to the SDHOST controller; GPIO 48-53
// on ALT3 connects it to the EMMC controller instead. No DMA and no
// interrupts: data moves word by word through the DATA register (PIO).
//
// Use from one core at a time only.

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "diskio.h"
#include "sdcard.h"
#include "hal/rpi.h"
#include "hal/gpio.h"
#include "hal/timer.h"
#include "storage.h"

/* ----- Registers ----- */
#define EMMC_BASE       (PERIPH_BASE + 0x300000)
#define EMMC_BLKSIZECNT REG32(EMMC_BASE + 0x04)
#define EMMC_ARG1       REG32(EMMC_BASE + 0x08)
#define EMMC_CMDTM      REG32(EMMC_BASE + 0x0C)
#define EMMC_RESP0      REG32(EMMC_BASE + 0x10)
#define EMMC_RESP1      REG32(EMMC_BASE + 0x14)
#define EMMC_RESP2      REG32(EMMC_BASE + 0x18)
#define EMMC_RESP3      REG32(EMMC_BASE + 0x1C)
#define EMMC_DATA       REG32(EMMC_BASE + 0x20)
#define EMMC_STATUS     REG32(EMMC_BASE + 0x24)
#define EMMC_CONTROL0   REG32(EMMC_BASE + 0x28)
#define EMMC_CONTROL1   REG32(EMMC_BASE + 0x2C)
#define EMMC_INTERRUPT  REG32(EMMC_BASE + 0x30)
#define EMMC_IRPT_MASK  REG32(EMMC_BASE + 0x34)
#define EMMC_IRPT_EN    REG32(EMMC_BASE + 0x38)
#define EMMC_SLOTISR_VER REG32(EMMC_BASE + 0xFC)

#define SR_CMD_INHIBIT  (1u << 0)
#define SR_DAT_INHIBIT  (1u << 1)

#define C0_HCTL_DWIDTH  (1u << 1)       /* 4-bit data bus */

#define C1_CLK_INTLEN   (1u << 0)
#define C1_CLK_STABLE   (1u << 1)
#define C1_CLK_EN       (1u << 2)
#define C1_TOUNIT_MAX   (0xEu << 16)
#define C1_SRST_HC      (1u << 24)
#define C1_SRST_CMD     (1u << 25)
#define C1_SRST_DATA    (1u << 26)

#define INT_CMD_DONE    (1u << 0)
#define INT_DATA_DONE   (1u << 1)
#define INT_WRITE_RDY   (1u << 4)
#define INT_READ_RDY    (1u << 5)
#define INT_ERR_MASK    0xFFFF8000u     /* all error bits */

/* ----- Commands (CMDTM) ----- */
#define CMD_IDX(n)      ((uint32_t)(n) << 24)
#define R_NONE          0
#define R_R1            0x001A0000u     /* 48 bits, CRC and index check */
#define R_R1B           0x001B0000u     /* same, with busy */
#define R_R2            0x00090000u     /* 136 bits, CRC check */
#define R_R3            0x00020000u     /* 48 bits, no check */
#define T_READ          ((1u << 21) | (1u << 4))
#define T_WRITE         (1u << 21)
#define T_MULTI         ((1u << 5) | (1u << 2) | (1u << 1))  /* + auto CMD12, block counter */

#define CMD0            (CMD_IDX(0)  | R_NONE)
#define CMD2            (CMD_IDX(2)  | R_R2)
#define CMD3            (CMD_IDX(3)  | R_R1)
#define CMD7            (CMD_IDX(7)  | R_R1B)
#define CMD8            (CMD_IDX(8)  | R_R1)
#define CMD9            (CMD_IDX(9)  | R_R2)
#define CMD16           (CMD_IDX(16) | R_R1)
#define CMD17           (CMD_IDX(17) | R_R1 | T_READ)
#define CMD18           (CMD_IDX(18) | R_R1 | T_READ | T_MULTI)
#define CMD24           (CMD_IDX(24) | R_R1 | T_WRITE)
#define CMD25           (CMD_IDX(25) | R_R1 | T_WRITE | T_MULTI)
#define CMD55           (CMD_IDX(55) | R_R1)
#define ACMD6           (CMD_IDX(6)  | R_R1)
#define ACMD41          (CMD_IDX(41) | R_R3)

/* ----- State ----- */
static uint32_t base_clock = 200000000u;
static int      host_v3;
static int      sdhc;                   /* block addressing (SDHC/SDXC) */
static uint32_t rca;
static uint32_t sectors;
static int      bus4;
static volatile DSTATUS state = STA_NOINIT;

void sdcard_set_base_clock(uint32_t hz)
{
    if (hz)
        base_clock = hz;
}

/* Wait until (reg & mask) == want, at most us microseconds. 0 = success. */
static int wait_reg(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t us)
{
    uint64_t end = timer_ticks() + timer_us_to_ticks(us);
    while ((*reg & mask) != want)
        if (timer_ticks() > end)
            return -1;
    return 0;
}

static void reset_line(uint32_t bit)
{
    EMMC_CONTROL1 |= bit;
    wait_reg(&EMMC_CONTROL1, bit, 0, 100000);
}

static int emmc_set_clock(uint32_t hz)
{
    if (wait_reg(&EMMC_STATUS, SR_CMD_INHIBIT | SR_DAT_INHIBIT, 0, 100000))
        return -1;

    EMMC_CONTROL1 &= ~C1_CLK_EN;
    delay_us(10);

    uint32_t field;
    if (host_v3) {                      /* 10-bit divider: f = base / (2 * div) */
        uint32_t div = (base_clock + 2 * hz - 1) / (2 * hz);
        if (div > 0x3FF)
            div = 0x3FF;
        field = ((div & 0xFF) << 8) | (((div >> 8) & 3) << 6);
    } else {                            /* 8-bit, powers of 2 */
        uint32_t n = 1;
        while (n < 0x80 && base_clock / (2 * n) > hz)
            n <<= 1;
        field = n << 8;
    }

    EMMC_CONTROL1 = (EMMC_CONTROL1 & ~0xFFC0u) | field | C1_CLK_INTLEN;
    if (wait_reg(&EMMC_CONTROL1, C1_CLK_STABLE, C1_CLK_STABLE, 100000))
        return -1;
    EMMC_CONTROL1 |= C1_CLK_EN;
    delay_us(10);
    return 0;
}

/* Send a command. resp may be NULL. 0 = success. */
static int sd_cmd(uint32_t cmd, uint32_t arg, uint32_t resp[4])
{
    uint32_t inhibit = SR_CMD_INHIBIT;
    if ((cmd & (1u << 21)) || (cmd & 0x00030000u) == 0x00030000u)
        inhibit |= SR_DAT_INHIBIT;
    if (wait_reg(&EMMC_STATUS, inhibit, 0, 100000))
        return -1;

    EMMC_INTERRUPT = EMMC_INTERRUPT;    /* clear all flags */
    EMMC_ARG1  = arg;
    EMMC_CMDTM = cmd;

    if (wait_reg(&EMMC_INTERRUPT, INT_CMD_DONE, INT_CMD_DONE, 100000) ||
        (EMMC_INTERRUPT & INT_ERR_MASK)) {
        EMMC_INTERRUPT = EMMC_INTERRUPT;
        reset_line(C1_SRST_CMD);
        return -1;
    }
    EMMC_INTERRUPT = INT_CMD_DONE;

    if (resp) {
        resp[0] = EMMC_RESP0;
        resp[1] = EMMC_RESP1;
        resp[2] = EMMC_RESP2;
        resp[3] = EMMC_RESP3;
    }

    if ((cmd & 0x00030000u) == 0x00030000u) {   /* R1b: wait until no longer busy */
        if (wait_reg(&EMMC_INTERRUPT, INT_DATA_DONE, INT_DATA_DONE, 500000))
            return -1;
        EMMC_INTERRUPT = INT_DATA_DONE;
    }
    return 0;
}

static int sd_acmd(uint32_t cmd, uint32_t arg, uint32_t resp[4])
{
    if (sd_cmd(CMD55, rca << 16, 0))
        return -1;
    return sd_cmd(cmd, arg, resp);
}

/* Number of 512-byte sectors from the CSD (R2: bits 127..8 in RESP3..0) */
static uint32_t csd_sectors(const uint32_t r[4])
{
    unsigned structure = (r[3] >> 22) & 3;
    if (structure == 1) {               /* CSD 2.0 (SDHC/SDXC) */
        uint32_t c_size = (r[1] >> 8) & 0x3FFFFF;
        return (c_size + 1) * 1024;
    }
    /* CSD 1.0 */
    uint32_t c_size = ((r[2] & 3) << 10) | (r[1] >> 22);
    uint32_t mult   = (r[1] >> 7) & 7;
    uint32_t bl_len = (r[2] >> 8) & 0xF;
    uint32_t blocks = (c_size + 1) << (mult + 2);
    return (blocks << bl_len) >> 9;
}

static void gpio_setup(void)
{
    /* GPIO 48 = CLK, 49 = CMD, 50-53 = DAT0-3; ALT3 = EMMC controller */
    for (unsigned pin = 48; pin <= 53; pin++) {
        gpio_fsel(pin, GPIO_ALT3);
        gpio_pull(pin, pin == 48 ? GPIO_PULL_NONE : GPIO_PULL_UP);
    }
}

void sd_init(void)
{
    state = STA_NOINIT;
}
void disk_init(void) __attribute__ ((weak, alias("sd_init")));

DSTATUS sd_status(BYTE drv)
{
    return drv == 0 ? state : (STA_NOINIT | STA_NODISK);
}
DSTATUS disk_status(BYTE drv) __attribute__ ((weak, alias("sd_status")));

DSTATUS sd_initialize(BYTE drv)
{
    uint32_t r[4];

    if (drv != 0)
        return STA_NOINIT | STA_NODISK;

    state = STA_NOINIT;
    disk_state = DISK_ERROR;
    sdhc = 0;
    rca = 0;
    bus4 = 0;

    gpio_setup();

    /* Reset the controller */
    EMMC_CONTROL0 = 0;
    EMMC_CONTROL1 |= C1_SRST_HC;
    if (wait_reg(&EMMC_CONTROL1, C1_SRST_HC, 0, 100000))
        return state;
    host_v3 = ((EMMC_SLOTISR_VER >> 16) & 0xFF) >= 2;

    EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;
    if (emmc_set_clock(400000))
        return state;

    EMMC_IRPT_EN   = 0;                 /* no interrupts, we poll */
    EMMC_IRPT_MASK = 0xFFFFFFFFu;
    EMMC_INTERRUPT = 0xFFFFFFFFu;

    /* Identification */
    if (sd_cmd(CMD0, 0, 0))
        return state;
    int v2 = sd_cmd(CMD8, 0x1AA, r) == 0 && (r[0] & 0xFFF) == 0x1AA;

    uint64_t end = timer_ticks() + timer_us_to_ticks(1000000);
    for (;;) {
        if (sd_acmd(ACMD41, 0x00FF8000u | (v2 ? 0x40000000u : 0), r) == 0 &&
            (r[0] & 0x80000000u)) {
            sdhc = v2 && (r[0] & 0x40000000u);
            break;
        }
        if (timer_ticks() > end)
            return state;
        delay_us(10000);
    }

    if (sd_cmd(CMD2, 0, r) || sd_cmd(CMD3, 0, r))
        return state;
    rca = r[0] >> 16;

    if (sd_cmd(CMD9, rca << 16, r))
        return state;
    sectors = csd_sectors(r);

    /* Data transfer: 25 MHz, select the card, 4-bit bus */
    if (emmc_set_clock(25000000))
        return state;
    if (sd_cmd(CMD7, rca << 16, 0))
        return state;
    if (sd_acmd(ACMD6, 2, 0) == 0) {
        EMMC_CONTROL0 |= C0_HCTL_DWIDTH;
        bus4 = 1;
    }
    if (!sdhc && sd_cmd(CMD16, 512, 0))
        return state;

    state = 0;
    disk_state = DISK_OK;
    return state;
}
DSTATUS disk_initialize(BYTE drv) __attribute__ ((weak, alias("sd_initialize")));

/* One read or write command of count blocks. 0 = success. */
static int transfer(BYTE *rbuf, const BYTE *wbuf, DWORD sector, BYTE count)
{
    uint32_t cmd;
    if (rbuf)
        cmd = count > 1 ? CMD18 : CMD17;
    else
        cmd = count > 1 ? CMD25 : CMD24;

    EMMC_BLKSIZECNT = ((uint32_t)count << 16) | 512;
    if (sd_cmd(cmd, sdhc ? sector : sector * 512, 0))
        return -1;

    uint32_t ready = rbuf ? INT_READ_RDY : INT_WRITE_RDY;
    for (unsigned b = 0; b < count; b++) {
        uint64_t end = timer_ticks() + timer_us_to_ticks(500000);
        uint32_t irq;
        while (!((irq = EMMC_INTERRUPT) & (ready | INT_ERR_MASK)))
            if (timer_ticks() > end)
                goto fail;
        if (irq & INT_ERR_MASK)
            goto fail;
        EMMC_INTERRUPT = ready;

        if (rbuf) {
            BYTE *p = rbuf + b * 512;
            for (unsigned i = 0; i < 128; i++) {
                uint32_t w = EMMC_DATA;
                p[0] = (BYTE)w;  p[1] = (BYTE)(w >> 8);
                p[2] = (BYTE)(w >> 16); p[3] = (BYTE)(w >> 24);
                p += 4;
            }
        } else {
            const BYTE *p = wbuf + b * 512;
            for (unsigned i = 0; i < 128; i++) {
                EMMC_DATA = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                p += 4;
            }
        }
    }

    if (wait_reg(&EMMC_INTERRUPT, INT_DATA_DONE, INT_DATA_DONE, 1000000))
        goto fail;
    if (EMMC_INTERRUPT & INT_ERR_MASK)
        goto fail;
    EMMC_INTERRUPT = INT_DATA_DONE;
    return 0;

fail:
    EMMC_INTERRUPT = EMMC_INTERRUPT;
    reset_line(C1_SRST_CMD);
    reset_line(C1_SRST_DATA);
    return -1;
}

static DRESULT transfer_retry(BYTE *rbuf, const BYTE *wbuf, DWORD sector, BYTE count)
{
    if (state & STA_NOINIT)
        return RES_NOTRDY;
    for (unsigned tries = 0; tries <= CONFIG_SD_AUTO_RETRIES; tries++)
        if (transfer(rbuf, wbuf, sector, count) == 0)
            return RES_OK;
    disk_state = DISK_ERROR;
    return RES_ERROR;
}

DRESULT sd_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count)
{
    if (drv != 0 || count == 0)
        return RES_PARERR;
    return transfer_retry(buffer, 0, sector, count);
}
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("sd_read")));

DRESULT sd_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count)
{
    if (drv != 0 || count == 0)
        return RES_PARERR;
    return transfer_retry(0, buffer, sector, count);
}
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("sd_write")));

DRESULT sd_getinfo(BYTE drv, BYTE page, void *buffer)
{
    if (drv != 0 || page != 0)
        return RES_ERROR;
    if (state & STA_NOINIT)
        return RES_NOTRDY;
    diskinfo0_t *di = buffer;
    di->validbytes  = sizeof(diskinfo0_t);
    di->maxpage     = 0;
    di->disktype    = DISK_TYPE_SD;
    di->sectorsize  = 2;
    di->sectorcount = sectors;
    return RES_OK;
}
DRESULT disk_getinfo(BYTE drv, BYTE page, void *buffer) __attribute__ ((weak, alias("sd_getinfo")));

/* For PiSD2IEC's console */
void sdcard_describe(int *is_sdhc, uint32_t *nsectors, int *buswidth4)
{
    *is_sdhc   = sdhc;
    *nsectors  = sectors;
    *buswidth4 = bus4;
}
