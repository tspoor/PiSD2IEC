// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// exception.c - register dump on an unexpected exception
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include <stdint.h>
#include "printf.h"
#include "timer.h"
#include "hat.h"
#include "rpi.h"

void exception_handler(unsigned type, uint64_t *regs);
void exceptions_init(void);

/* Vector table: 4 groups of 4 (current EL with SP0, current EL with SPx,
   lower EL in AArch64, lower EL in AArch32). We run with SPx. */
static const char *const exc_names[16] = {
    "Sync SP0", "IRQ SP0", "FIQ SP0", "SError SP0",
    "Sync", "IRQ", "FIQ", "SError",
    "Sync lower EL/64", "IRQ lower EL/64", "FIQ lower EL/64", "SError lower EL/64",
    "Sync lower EL/32", "IRQ lower EL/32", "FIQ lower EL/32", "SError lower EL/32",
};

extern char exception_vectors[];        /* vectors.S */

/* Each core calls this itself, as early as possible (VBAR is per core). */
void exceptions_init(void)
{
    if (current_el() == 2)
        __asm__ volatile("msr vbar_el2, %0\n isb" :: "r"(exception_vectors) : "memory");
    else
        __asm__ volatile("msr vbar_el1, %0\n isb" :: "r"(exception_vectors) : "memory");
}

void exception_handler(unsigned type, uint64_t *regs)
{
    uint64_t esr, elr, far, spsr;
    unsigned el = current_el();
    if (el == 2) {
        __asm__ volatile("mrs %0, esr_el2"  : "=r"(esr));
        __asm__ volatile("mrs %0, elr_el2"  : "=r"(elr));
        __asm__ volatile("mrs %0, far_el2"  : "=r"(far));
        __asm__ volatile("mrs %0, spsr_el2" : "=r"(spsr));
    } else {
        __asm__ volatile("mrs %0, esr_el1"  : "=r"(esr));
        __asm__ volatile("mrs %0, elr_el1"  : "=r"(elr));
        __asm__ volatile("mrs %0, far_el1"  : "=r"(far));
        __asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
    }

    kprintf_panic();
    kprintf("\n*** EXCEPTION on core %u (EL%u): %s\n", core_id(), el, exc_names[type & 15]);
    kprintf("ESR=%016lx (EC=0x%02lx)  ELR=%016lx\n", esr, (esr >> 26) & 0x3f, elr);
    kprintf("FAR=%016lx  SPSR=%016lx\n", far, spsr);
    for (unsigned i = 0; i < 31; i++)
        kprintf("x%02u=%016lx%s", i, regs[i], (i % 3 == 2 || i == 30) ? "\n" : "  ");
    kprintf("*** System halted.\n");

    for (;;) {                           /* fast blinking = halted */
        pi_act_led(1);
        delay_ms(80);
        pi_act_led(0);
        delay_ms(80);
    }
}
