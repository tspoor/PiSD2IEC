// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// i2c.c - I2C master on BSC0 or BSC1 (HAT_I2C_BUS in hat.h)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "rpi.h"
#include "gpio.h"
#include "hat.h"
#include "timer.h"
#include "i2c.h"

/* BSC0 (GPIO 0/1) or BSC1 (GPIO 2/3), selected with HAT_I2C_BUS */
#if HAT_I2C_BUS == 0
#define BSC_BASE    (PERIPH_BASE + 0x205000)
#else
#define BSC_BASE    (PERIPH_BASE + 0x804000)
#endif
#define BSC_C       REG32(BSC_BASE + 0x00)
#define BSC_S       REG32(BSC_BASE + 0x04)
#define BSC_DLEN    REG32(BSC_BASE + 0x08)
#define BSC_A       REG32(BSC_BASE + 0x0C)
#define BSC_FIFO    REG32(BSC_BASE + 0x10)
#define BSC_DIV     REG32(BSC_BASE + 0x14)

#define C_I2CEN     (1u << 15)
#define C_ST        (1u << 7)
#define C_CLEAR     (3u << 4)           /* clear the FIFO */

#define S_TA        (1u << 0)           /* transfer active */
#define S_DONE      (1u << 1)
#define S_TXD       (1u << 4)           /* FIFO can accept data */
#define S_ERR       (1u << 8)           /* NACK */
#define S_CLKT      (1u << 9)           /* clock-stretch-time-out */

static volatile unsigned last_err;

unsigned i2c_last_error(void)
{
    return last_err;
}

const char *i2c_error_str(unsigned err)
{
    switch (err) {
    case I2C_OK:      return "ok";
    case I2C_NACK:    return "no answer (NACK)";
    case I2C_CLKT:    return "clock-stretch-time-out";
    case I2C_TIMEOUT: return "time-out (bus stuck?)";
    default:          return "unknown";
    }
}

int i2c_probe(uint8_t addr)
{
    return i2c_write(addr, 0x00, 0, 0);
}

void i2c_init(uint32_t core_hz, uint32_t bus_hz)
{
#if HAT_I2C_BUS == 0
    /* GPIO 0/1 have no fixed pull-ups on the Pi; the OLED board usually
       does. The internal pull-ups (~50k) are a fallback. */
    gpio_pull(HAT_I2C_SDA, GPIO_PULL_UP);
    gpio_pull(HAT_I2C_SCL, GPIO_PULL_UP);
#else
    gpio_pull(HAT_I2C_SDA, GPIO_PULL_NONE);  /* the Pi has 1.8k pull-ups on GPIO 2/3 */
    gpio_pull(HAT_I2C_SCL, GPIO_PULL_NONE);
#endif
    gpio_fsel(HAT_I2C_SDA, GPIO_ALT0);
    gpio_fsel(HAT_I2C_SCL, GPIO_ALT0);

    if (!core_hz)
        core_hz = 250000000u;
    BSC_C   = 0;
    BSC_DIV = core_hz / bus_hz;
    BSC_S   = S_DONE | S_ERR | S_CLKT;
}

int i2c_write(uint8_t addr, uint8_t first, const uint8_t *data, unsigned len)
{
    unsigned total = len + 1;
    if (total > 65535)
        return 0;

    /* The previous transaction must really be finished (after a NACK the
       controller still sends a STOP while DONE is already set). */
    uint64_t idle = timer_ticks() + timer_us_to_ticks(2000);
    while ((BSC_S & S_TA) && timer_ticks() < idle)
        ;

    BSC_C    = C_I2CEN | C_CLEAR;
    BSC_S    = S_DONE | S_ERR | S_CLKT;
    BSC_A    = addr;
    BSC_DLEN = total;

    unsigned sent = 0;
#define NEXT_BYTE() (sent == 0 ? first : data[sent - 1])

    while (sent < total && (BSC_S & S_TXD)) {   /* pre-fill the FIFO (16 bytes) */
        BSC_FIFO = NEXT_BYTE();
        sent++;
    }
    BSC_C = C_I2CEN | C_ST;

    /* generous: ~23 us per byte at 400 kHz, plus margin */
    uint64_t deadline = timer_ticks() + timer_us_to_ticks(20000 + total * 50);
    uint32_t s;
    while (!((s = BSC_S) & S_DONE)) {
        while (sent < total && (BSC_S & S_TXD)) {
            BSC_FIFO = NEXT_BYTE();
            sent++;
        }
        if ((s & (S_ERR | S_CLKT)) || timer_ticks() > deadline)
            break;
    }
#undef NEXT_BYTE

    /* Wait until the STOP has been sent too */
    idle = timer_ticks() + timer_us_to_ticks(2000);
    while ((BSC_S & S_TA) && timer_ticks() < idle)
        ;

    s = BSC_S;
    BSC_S = S_DONE | S_ERR | S_CLKT;
    int ok = (s & S_DONE) && !(s & (S_ERR | S_CLKT)) && sent == total;
    if (!ok)
        BSC_C = C_I2CEN | C_CLEAR;      /* abort and clear the FIFO */
    last_err = ok ? I2C_OK
             : (s & S_ERR)  ? I2C_NACK
             : (s & S_CLKT) ? I2C_CLKT
             : I2C_TIMEOUT;
    return ok;
}
