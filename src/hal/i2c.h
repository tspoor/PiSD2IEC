// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// i2c.h - I2C master on BSC0 or BSC1 (see HAT_I2C_BUS in hat.h), write only
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/*
 * Set up pins and clock speed. Core 0 only, before the other cores start
 * (gpio_fsel). core_hz: VPU core clock (mailbox, usually 250 MHz).
 */
void i2c_init(uint32_t core_hz, uint32_t bus_hz);

/*
 * Write 'first' followed by len bytes from data to 7-bit address addr, in
 * one transaction (max. 65534 bytes). Returns 1 on success, 0 on NACK, a
 * time-out or a clock-stretch time-out. Waits until the transaction is done.
 */
int i2c_write(uint8_t addr, uint8_t first, const uint8_t *data, unsigned len);

/* Reason for the last failed i2c_write()/i2c_probe() */
enum { I2C_OK = 0, I2C_NACK, I2C_CLKT, I2C_TIMEOUT };
unsigned    i2c_last_error(void);
const char *i2c_error_str(unsigned err);

/* 1 if a device answers at addr (sends one byte 0x00) */
int i2c_probe(uint8_t addr);

#endif
