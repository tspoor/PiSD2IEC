// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// power.h - reboot the Pi through the watchdog
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef POWER_H
#define POWER_H

/*
 * Full reset of the chip, just like power-on: the firmware then boots from
 * the SD card again (with imgrecv, raspbootcom then reloads the kernel
 * automatically). Does not return. May be called from any core.
 */
void pi_reboot(void) __attribute__((noreturn));

#endif
