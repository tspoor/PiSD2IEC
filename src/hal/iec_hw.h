// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// iec_hw.h - fast access to the IEC bus lines
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Convention (as in sd2iec): 1 = line high/released, 0 = line low.
// All pins are in GPIO bank 0, so one register read gives the state of
// the whole bus.

#ifndef IEC_HW_H
#define IEC_HW_H

#include <stdint.h>
#include "rpi.h"
#include "hat.h"

#define IEC_BIT_ATN     (1u << 0)
#define IEC_BIT_CLOCK   (1u << 1)
#define IEC_BIT_DATA    (1u << 2)
#define IEC_BIT_SRQ     (1u << 3)
#define IEC_BIT_RESET   (1u << 4)

#define IEC_OMASK_ATN   (1u << HAT_OUT_ATN)
#define IEC_OMASK_CLOCK (1u << HAT_OUT_CLOCK)
#define IEC_OMASK_DATA  (1u << HAT_OUT_DATA)
#define IEC_OMASK_SRQ   (1u << HAT_OUT_SRQ)
#define IEC_OMASK_ALL   (IEC_OMASK_ATN | IEC_OMASK_CLOCK | IEC_OMASK_DATA | IEC_OMASK_SRQ)

static inline uint32_t iec_raw(void)
{
#if HAT_IEC_IN_INVERTED
    return ~GPLEV0;
#else
    return GPLEV0;
#endif
}

/* State of all lines as an IEC_BIT_* mask */
static inline unsigned iec_read(void)
{
    uint32_t v = iec_raw();
    return (((v >> HAT_IN_ATN)   & 1u) << 0) |
           (((v >> HAT_IN_CLOCK) & 1u) << 1) |
           (((v >> HAT_IN_DATA)  & 1u) << 2) |
           (((v >> HAT_IN_SRQ)   & 1u) << 3) |
           (((v >> HAT_IN_RESET) & 1u) << 4);
}

static inline unsigned iec_atn(void)   { return (iec_raw() >> HAT_IN_ATN)   & 1u; }
static inline unsigned iec_clock(void) { return (iec_raw() >> HAT_IN_CLOCK) & 1u; }
static inline unsigned iec_data(void)  { return (iec_raw() >> HAT_IN_DATA)  & 1u; }
static inline unsigned iec_reset(void) { return (iec_raw() >> HAT_IN_RESET) & 1u; }

/* released != 0: release the line, 0: pull the line low */
static inline void iec_out(uint32_t mask, int released)
{
#if HAT_IEC_OUT_INVERTED
    if (released) GPCLR0 = mask; else GPSET0 = mask;
#else
    if (released) GPSET0 = mask; else GPCLR0 = mask;
#endif
}

static inline void iec_set_atn(int r)   { iec_out(IEC_OMASK_ATN, r); }
static inline void iec_set_clock(int r) { iec_out(IEC_OMASK_CLOCK, r); }
static inline void iec_set_data(int r)  { iec_out(IEC_OMASK_DATA, r); }
static inline void iec_set_srq(int r)   { iec_out(IEC_OMASK_SRQ, r); }

void iec_hw_init(void);

#endif
