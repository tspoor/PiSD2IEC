// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 T. Spoor
// Based on sd2iec (src/lpc17xx/iec-bus.c),
// Copyright (C) 2007-2026 Ingo Korb <ingo@akana.de>
//
// iec-bus.c - initialise the IEC bus (like sd2iec's lpc17xx/iec-bus.c)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Release all lines. Core 0 already sets the pin functions at startup
// (hal/iec_hw.c), so iec_pins_connect() does nothing here.

#include "config.h"
#include "iec-bus.h"

void iec_interface_init(void)
{
    set_atn(1);
    set_data(1);
    set_clock(1);
    set_srq(1);

    iec_pins_connect();
    parallel_init();
}
void bus_interface_init(void) __attribute__ ((weak, alias("iec_interface_init")));
