// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// mailbox.c - channel 8 (property tags)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "rpi.h"
#include "mmu.h"
#include "mailbox.h"

#define MBOX_CH_PROP     8
#define MBOX_REQUEST     0u
#define MBOX_RESPONSE_OK 0x80000000u
#define GPU_BUS_ALIAS    0xC0000000u    /* ARM RAM as seen from the VideoCore */

static volatile uint32_t mbox_buf[16] __attribute__((aligned(64)));

static int mbox_call(unsigned channel)
{
    uint32_t addr = (uint32_t)(uintptr_t)mbox_buf;
    uint32_t msg  = (addr | GPU_BUS_ALIAS) | (channel & 0xF);

    dcache_clean_inval_range(mbox_buf, sizeof(mbox_buf));

    while (MBOX_STATUS & MBOX_FULL)
        ;
    dmb();
    MBOX_WRITE = msg;

    for (;;) {
        while (MBOX_STATUS & MBOX_EMPTY)
            ;
        uint32_t r = MBOX_READ;
        dmb();
        if (r == msg) {
            dcache_clean_inval_range(mbox_buf, sizeof(mbox_buf));
            return mbox_buf[1] == MBOX_RESPONSE_OK;
        }
    }
}

/* One tag with at most 3 values. Returns 1 on success. */
static int mbox_tag(uint32_t tag, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t out[3])
{
    mbox_buf[0] = 9 * 4;
    mbox_buf[1] = MBOX_REQUEST;
    mbox_buf[2] = tag;
    mbox_buf[3] = 12;
    mbox_buf[4] = 0;
    mbox_buf[5] = a0;
    mbox_buf[6] = a1;
    mbox_buf[7] = a2;
    mbox_buf[8] = 0;                     /* end tag */

    if (!mbox_call(MBOX_CH_PROP) || !(mbox_buf[4] & 0x80000000u))
        return 0;
    out[0] = mbox_buf[5];
    out[1] = mbox_buf[6];
    out[2] = mbox_buf[7];
    return 1;
}

uint32_t mbox_get_board_revision(void)
{
    uint32_t o[3];
    return mbox_tag(0x00010002, 0, 0, 0, o) ? o[0] : 0;
}

int mbox_get_arm_memory(uint32_t *base, uint32_t *size)
{
    uint32_t o[3];
    if (!mbox_tag(0x00010005, 0, 0, 0, o))
        return 0;
    *base = o[0];
    *size = o[1];
    return 1;
}

uint32_t mbox_get_clock_rate(uint32_t clock_id)
{
    uint32_t o[3];
    return mbox_tag(0x00030002, clock_id, 0, 0, o) ? o[1] : 0;
}

uint32_t mbox_get_max_clock_rate(uint32_t clock_id)
{
    uint32_t o[3];
    return mbox_tag(0x00030004, clock_id, 0, 0, o) ? o[1] : 0;
}

uint32_t mbox_set_clock_rate(uint32_t clock_id, uint32_t hz)
{
    uint32_t o[3];
    return mbox_tag(0x00038002, clock_id, hz, 0, o) ? o[1] : 0;
}
