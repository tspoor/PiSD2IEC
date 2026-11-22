// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// main.c - core 0: startup, starting the other cores, console
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Division of work: see cores.h. Core 1 runs sd2iec (IEC bus, DOS, FAT,
// SD card); core 0 puts sd2iec's debug output on the console and handles
// keys (including 'r' to reboot the Pi).

#include <stdint.h>
#include "rpi.h"
#include "mmu.h"
#include "timer.h"
#include "uart.h"
#include "mailbox.h"
#include "hat.h"
#include "iec_hw.h"
#include "smp.h"
#include "i2c.h"
#include "gpio.h"
#include "power.h"
#include "sd2iec/rpi/storage.h"
#include "cores.h"
#include "printf.h"

void main_core0(void);                  /* via boot0.S (kernel_main) */

/* Queues between the cores (in .bss, so empty at startup) */
struct ring q_ui_to_sys;
struct ring q_sys_to_ui;

static const char *const core_names[4] = {
    "system    ", "sd2iec    ", "ui        ", "tick+ATN  "
};

/* Is the Pi pulling this IEC output low? (through the 7406: GPIO high = low) */
static unsigned pulled(uint32_t lev, unsigned pin)
{
    unsigned hi = (lev >> pin) & 1u;
    return HAT_IEC_OUT_INVERTED ? hi : !hi;
}

static void print_bus(unsigned b)
{
    uint32_t lev = GPLEV0;
    kprintf("Bus         : ATN=%u CLK=%u DATA=%u SRQ=%u RESET=%u (1 = released)\n",
            !!(b & IEC_BIT_ATN), !!(b & IEC_BIT_CLOCK), !!(b & IEC_BIT_DATA),
            !!(b & IEC_BIT_SRQ), !!(b & IEC_BIT_RESET));
    kprintf("Pi pulls    : ATN=%u CLK=%u DATA=%u (1 = Pi pulls low)\n",
            pulled(lev, HAT_OUT_ATN), pulled(lev, HAT_OUT_CLOCK), pulled(lev, HAT_OUT_DATA));
}

/* Most recent changes from core 3's bus trace */
static void print_trace(unsigned max)
{
    uint32_t head = bustrace_head();
    uint32_t n = head < max ? head : max;
    uint64_t f = timer_freq(), prev = 0;

    kprintf("Bus trace, last %u changes. Bus: 1 = released. Pi: 1 = Pi pulls low.\n", n);
    kprintf("   dt (us)   bus ATN CLK DATA    Pi ATN CLK DATA\n");
    for (uint32_t i = head - n; i != head; i++) {
        uint64_t t;
        uint32_t v;
        bustrace_entry(i, &t, &v);
        unsigned long dt10 = prev ? (unsigned long)((t - prev) * 10000000u / f) : 0;
        prev = t;
        kprintf("%8lu.%lu        %u   %u   %u          %u   %u   %u\n",
                dt10 / 10, dt10 % 10,
                (v >> HAT_IN_ATN) & 1u, (v >> HAT_IN_CLOCK) & 1u, (v >> HAT_IN_DATA) & 1u,
                pulled(v, HAT_OUT_ATN), pulled(v, HAT_OUT_CLOCK), pulled(v, HAT_OUT_DATA));
    }
}

static void print_cores(void)
{
    for (unsigned c = 0; c < 4; c++)
        kprintf("Core %u      : %s %s\n", c, core_names[c],
                smp_core_running(c) ? "running" : "parked");
}

static void print_info(void)
{
    uint32_t base = 0, size = 0;
    mbox_get_arm_memory(&base, &size);

    kprintf("\nSYSTEM\n");
    kprintf("Board rev.  : 0x%06x\n", mbox_get_board_revision());
    kprintf("Exc. level  : EL%u, core %u\n", current_el(), core_id());
    kprintf("ARM clock   : %u MHz\n", mbox_get_clock_rate(MBOX_CLK_ARM) / 1000000);
    kprintf("Core clock  : %u MHz\n", mbox_get_clock_rate(MBOX_CLK_CORE) / 1000000);
    kprintf("ARM memory  : 0x%08x, %u MB\n", base, size >> 20);
    kprintf("Timer       : %lu Hz\n", (unsigned long)timer_freq());
    if (timer_freq() != 19200000)
        kprintf("              (fast loaders expect 19200000 Hz!)\n");

    kprintf("\nCORES\n");
    print_cores();
}

static void print_help(void)
{
    kprintf(
        "\nConsole commands:\n"
        "  s       Bus state\n"
        "  t       Bus trace\n"
        "  d       1541 emulation state\n"
        "  w       1541 watch address\n"
        "  l / p   1541 register log\n"
        "  x       Trace in 1541 mode\n"
        "  b       Beep\n"
        "  h       Help\n"
        "  i       System info\n"
        "  r       Reboot\n"
        "\n"
    );
}

static void print_sd2iec(void)
{
    int is_sdhc, bus4;
    uint32_t nsec;
    sdcard_describe(&is_sdhc, &nsec, &bus4);

    kprintf("\nSD2IEC\n");

    if (sd2iec_disk_ok())
        kprintf("SD card     : %s, %lu MB, %u-bit bus\n",
                is_sdhc ? "SDHC/SDXC" : "SDSC",
                (unsigned long)(nsec / 2048), bus4 ? 4 : 1);
    else
        kprintf("SD card     : not found or error\n");
    kprintf("sd2iec      : ready, device %u\n", sd2iec_device_address());
    kprintf("Fast loader : %u late time slots\n", (unsigned)llfl_late_count);
}

/*
 * Cold reboot: full chip reset through the watchdog, just like power-on.
 * First the message on the console and (through core 2) on the OLED, then
 * the reset. The firmware then boots from the SD card again; with imgrecv,
 * raspbootcom reads build/kernel8.img from disk again, so a new build is
 * loaded straight away.
 */
static void reboot(void)
{
    kprintf("\nCold Reboot.\n");

    if (smp_core_running(CORE_UI) &&
        ring_put(&q_sys_to_ui, MSG_REBOOT, 0, timer_ticks())) {
        uint64_t end = timer_ticks() + timer_us_to_ticks(300000);
        struct msg m;
        while (timer_ticks() < end)
            if (ring_get(&q_ui_to_sys, &m) && m.type == MSG_REBOOT_OK)
                break;
    }

    while (UART0_FR & UART_FR_BUSY)
        ;
    pi_reboot();
}

/* I2C bus: line levels (idle = 1) and which addresses answer */
static void i2c_report(void)
{
    kprintf("I2C         : SDA=%u SCL=%u (idle = 1), found:",
            gpio_read(HAT_I2C_SDA), gpio_read(HAT_I2C_SCL));
    unsigned found = 0, err = I2C_OK;
    for (unsigned a = 0x08; a <= 0x77; a++) {
        if (i2c_probe((uint8_t)a)) {
            kprintf(" 0x%02x", a);
            found++;
        } else if (i2c_last_error() != I2C_NACK) {
            err = i2c_last_error();
        }
    }
    if (!found)
        kprintf(" nothing");
    if (err != I2C_OK)
        kprintf(" (error: %s)", i2c_error_str(err));
    kprintf("\n");
}

/* Report when a queue has had to drop messages */
static void check_dropped(const char *name, const struct ring *r, uint32_t *seen)
{
    uint32_t d = ring_dropped(r);
    if (d != *seen) {
        kprintf("Warning: queue %s full, %u message(s) lost\n", name, d - *seen);
        *seen = d;
    }
}

/* 'w': read four hex digits and snapshot the 1541 there ('d' shows it) */
static void read_watch(void)
{
    unsigned pc = 0, n = 0;
    kprintf("1541 watch address (4 hex digits): ");
    uint64_t until = timer_ticks() + timer_us_to_ticks(10000000);
    while (n < 4 && timer_ticks() < until) {
        emu1541_worker();               /* keep a disk change going meanwhile */
        int c = uart_getc();
        if (c < 0)
            continue;
        int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0)
            break;
        uart_putc((char)c);
        pc = pc << 4 | (unsigned)v;
        n++;
    }
    if (n < 4) {
        kprintf("\nwatch off\n");
        emu1541_set_watch(0);
        return;
    }
    kprintf("\nsnapshot at the first $%04X\n", pc);
    emu1541_set_watch((uint16_t)pc);
}

/* Key 'l': up to 4 addresses (4 hex digits each, separated by spaces), Enter */
static void read_log(void)
{
    uint16_t pcs[4];
    unsigned n = 0, digits = 0, pc = 0;
    kprintf("1541 log addresses (e.g. 03E0 03E2, Enter; none = off): ");
    uint64_t until = timer_ticks() + timer_us_to_ticks(20000000);
    while (timer_ticks() < until) {
        emu1541_worker();               /* keep a disk change going meanwhile */
        int c = uart_getc();
        if (c < 0)
            continue;
        int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v >= 0 && digits < 4) {
            uart_putc((char)c);
            pc = pc << 4 | (unsigned)v;
            if (++digits == 4 && n < 4) {
                pcs[n++] = (uint16_t)pc;
                pc = 0;
                digits = 0;
            }
        } else if (c == ' ') {
            uart_putc(' ');
        } else {
            break;                      /* Enter (or anything else) */
        }
    }
    emu1541_set_log(pcs, n);
    if (n)
        kprintf("\nlogging %u address(es); 'p' prints the log\n", n);
    else
        kprintf("\nlog off\n");
}

void exceptions_init(void);             /* hal/exception.c */

void main_core0(void)
{
    exceptions_init();
    timer_init();
    mmu_init();

    mbox_set_clock_rate(MBOX_CLK_UART, UART_CLOCK_HZ);
    uart_init(115200);

    kprintf("\n\nBooting PiSD2IEC v%s\n\n", PISD2IEC_VERSION);

    /* ARM at maximum clock speed (1000 MHz by default on the Zero 2 W) */
    uint32_t arm_max = mbox_get_max_clock_rate(MBOX_CLK_ARM);
    if (arm_max)
        mbox_set_clock_rate(MBOX_CLK_ARM, arm_max);

    /* Set up all pins before the other cores start: gpio_fsel() is a
       read-modify-write and must not run on two cores at the same time. */
    hat_init();
    iec_hw_init();
    i2c_init(mbox_get_clock_rate(MBOX_CLK_CORE), 400000);   /* OLED, 400 kHz */
    i2c_report();                       /* before core 2 starts using the bus */
    sdcard_set_base_clock(mbox_get_clock_rate(MBOX_CLK_EMMC));
    hat_beep(2000, 80);

    /* Core 3 first (tick and ATN; sd2iec waits on 'ticks'), then sd2iec */
    if (!smp_start_core(CORE_TICK, sd2iec_tick_main))
        kprintf("ERROR: core %u (tick+ATN) did not start\n", CORE_TICK);
    if (!smp_start_core(CORE_IEC, sd2iec_core_main))
        kprintf("ERROR: core %u (sd2iec) did not start\n", CORE_IEC);
    if (!smp_start_core(CORE_UI, ui_core_main))
        kprintf("ERROR: core %u (ui) did not start\n", CORE_UI);

    print_info();

    uint32_t drop_btn = 0, drop_beep = 0;
    int ready_shown = 0;

    for (;;) {
        struct msg m;

        /* SD card work for the 1541 emulation on core 1 */
        emu1541_worker();

        /* sd2iec's debug output from core 1 */
        for (int c; (c = sd2iec_log_getc()) >= 0; )
            uart_putc((char)c);

        if (!ready_shown && sd2iec_is_ready()) {
            print_sd2iec();
            kprintf("\nConsole: press 'h' for help\n");
            ready_shown = 1;
        }

        while (ring_get(&q_ui_to_sys, &m))
            if (m.type == MSG_BUTTON)
                kprintf("Button %u %s\n", (m.arg & 0xff) + 1,
                        (m.arg >> 8) ? "pressed" : "released");
            else if (m.type == MSG_OLED && (m.arg & 0xff))
                kprintf("\nOLED        : SSD1306 128x64 at I2C address 0x%02x\n", m.arg & 0xff);
            else if (m.type == MSG_OLED)
                kprintf("\nOLED        : not found at 0x3C/0x3D: %s\n",
                        i2c_error_str(m.arg >> 8));

        check_dropped("ui->system", &q_ui_to_sys, &drop_btn);
        check_dropped("system->ui", &q_sys_to_ui, &drop_beep);

        switch (uart_getc()) {
        case 's': print_bus(iec_read());                               break;
        case 'b': ring_put(&q_sys_to_ui, MSG_BEEP, 2000, timer_ticks()); break;
        case 'h':
        case '?':
            print_help(); break;
        case 'i': print_info(); if (sd2iec_is_ready()) print_sd2iec(); break;
        case 'r': reboot();                                              break;
        case 't': print_trace(300);                                    break;
        case 'd': emu1541_dump(kprintf);                               break;
        case 'w': read_watch();                                        break;
        case 'l': read_log();                                          break;
        case 'p': emu1541_print_log(kprintf);                          break;
        case 'x': rpi_trace_in_emu = !rpi_trace_in_emu;
                  kprintf("Bus trace during 1541 emulation: %s\n", rpi_trace_in_emu ? "on" : "off");
                  break;
        default:                                                       break;
        }
    }
}
