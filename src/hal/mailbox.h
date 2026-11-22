// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// mailbox.h - property interface of the VideoCore firmware
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>

enum {
    MBOX_CLK_EMMC = 1,
    MBOX_CLK_UART = 2,
    MBOX_CLK_ARM  = 3,
    MBOX_CLK_CORE = 4,
};

uint32_t mbox_get_board_revision(void);
int      mbox_get_arm_memory(uint32_t *base, uint32_t *size);
uint32_t mbox_get_clock_rate(uint32_t clock_id);
uint32_t mbox_get_max_clock_rate(uint32_t clock_id);
uint32_t mbox_set_clock_rate(uint32_t clock_id, uint32_t hz);

#endif
