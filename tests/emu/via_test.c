// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// via_test.c - checks of the 6522 timing (src/emu/via6522.h), on the PC
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// These encode the behaviour of the datasheet's timing diagrams cycle by
// cycle. Each "cycle" is: optional CPU access, then via_tick().

#include <stdio.h>
#include <string.h>
#include "../../src/emu/via6522.h"

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static struct via6522 v;

static void fresh(void)
{
    memset(&v, 0, sizeof(v));
    via_reset(&v);
}

/* A cycle in which the CPU reads a register */
static uint8_t cyc_read(unsigned reg)
{
    uint8_t r = via_read(&v, reg);
    via_tick(&v);
    return r;
}

static void cyc_write(unsigned reg, uint8_t val)
{
    via_write(&v, reg, val);
    via_tick(&v);
}

static uint16_t t1_now(void)
{
    /* Peek without side effects (T1C-L read would clear the flag) */
    return v.t1c;
}

static void test_t1_oneshot(void)
{
    fresh();
    cyc_write(0x4, 5);
    cyc_write(0x5, 0);                  /* cycle 0: start with N = 5 */
    const uint16_t want[] = { 5, 4, 3, 2, 1, 0, 0xffff, 0xfffe, 0xfffd };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK(t1_now() == want[i], "T1 one-shot cycle %u: %04X, expected %04X", i + 1, t1_now(), want[i]);
        int flag = (v.ifr & VIA_IF_T1) != 0;
        CHECK(flag == (i >= 6), "T1 flag at cycle %u: %d", i + 1, flag);
        via_tick(&v);
    }
    /* No second interrupt in one-shot mode */
    v.ifr = 0;
    for (unsigned i = 0; i < 70000; i++)
        via_tick(&v);
    CHECK(!(v.ifr & VIA_IF_T1), "T1 one-shot fired twice");
}

static void test_t1_freerun(void)
{
    fresh();
    cyc_write(0xb, 0x40);               /* ACR: T1 free-run */
    cyc_write(0x4, 3);
    cyc_write(0x5, 0);
    const uint16_t want[] = { 3, 2, 1, 0, 0xffff, 3, 2, 1, 0, 0xffff, 3 };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK(t1_now() == want[i], "T1 free-run cycle %u: %04X, expected %04X", i + 1, t1_now(), want[i]);
        via_tick(&v);
    }
}

static void test_t1_irq_timing(void)
{
    fresh();
    cyc_write(0xe, 0x80 | VIA_IF_T1);   /* enable T1 interrupt */
    cyc_write(0x4, 2);
    cyc_write(0x5, 0);                  /* cycle 0 */
    /* N + 1.5: the IRQ is asserted at the end of cycle N + 2 */
    int irq_at = -1;
    for (int i = 1; i < 10 && irq_at < 0; i++) {
        via_tick(&v);
        if (via_irq(&v))
            irq_at = i;
    }
    CHECK(irq_at == 3, "T1 IRQ after cycle %d, expected 3 (N+1)", irq_at);
    /* Reading T1C-L clears it */
    cyc_read(0x4);
    CHECK(!via_irq(&v), "T1C-L read did not clear the IRQ");
}

static void test_t2(void)
{
    fresh();
    cyc_write(0x8, 4);
    cyc_write(0x9, 0);
    const uint16_t want[] = { 4, 3, 2, 1, 0, 0xffff, 0xfffe };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK(v.t2c == want[i], "T2 cycle %u: %04X, expected %04X", i + 1, v.t2c, want[i]);
        CHECK(((v.ifr & VIA_IF_T2) != 0) == (i >= 5), "T2 flag at cycle %u", i + 1);
        via_tick(&v);
    }
}

static void test_ca1_and_ports(void)
{
    fresh();
    v.ca1 = 1;
    via_set_ca1(&v, 0);                 /* PCR bit 0 = 0: falling edge */
    CHECK(v.ifr & VIA_IF_CA1, "CA1 falling edge not seen");
    cyc_read(0x1);                      /* ORA read clears it */
    CHECK(!(v.ifr & VIA_IF_CA1), "ORA read did not clear CA1");
    cyc_write(0xc, 0x01);               /* rising edge now */
    via_set_ca1(&v, 1);
    CHECK(v.ifr & VIA_IF_CA1, "CA1 rising edge not seen");

    cyc_write(0x2, 0x0f);               /* DDRB: low nibble out */
    cyc_write(0x0, 0x05);
    via_set_pb(&v, 0xa0);
    uint8_t pb = cyc_read(0x0);
    CHECK(pb == 0xa5, "IRB = %02X, expected A5", pb);
    CHECK(via_pb_out(&v) == 0xf5, "PB out = %02X, expected F5", via_pb_out(&v));

    cyc_write(0xe, 0x80 | VIA_IF_CA1);
    uint8_t ifr = cyc_read(0xd);
    CHECK(ifr == 0x82, "IFR = %02X, expected 82", ifr);
    cyc_write(0xd, 0x7f);
    CHECK(!via_irq(&v), "IFR write did not clear");
}

int main(void)
{
    test_t1_oneshot();
    test_t1_freerun();
    test_t1_irq_timing();
    test_t2();
    test_ca1_and_ports();
    printf("VIA: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
