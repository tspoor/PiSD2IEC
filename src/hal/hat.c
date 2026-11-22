// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// hat.c - the HAT's LEDs, buttons and buzzer
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include "gpio.h"
#include "timer.h"
#include "hat.h"

static const unsigned char button_pins[HAT_NUM_BUTTONS] = {
    HAT_BUTTON1, HAT_BUTTON2, HAT_BUTTON3, HAT_BUTTON4, HAT_BUTTON5
};

void hat_init(void)
{
    gpio_clr(HAT_LED);
    gpio_fsel(HAT_LED, GPIO_OUT);
    gpio_clr(HAT_BUZZER);
    gpio_fsel(HAT_BUZZER, GPIO_OUT);
    gpio_fsel(PI_ACT_LED, GPIO_OUT);
    pi_act_led(0);

    for (unsigned i = 0; i < HAT_NUM_BUTTONS; i++) {
        gpio_fsel(button_pins[i], GPIO_IN);
        gpio_pull(button_pins[i], GPIO_PULL_UP);
    }
}

unsigned hat_buttons(void)
{
    uint32_t lev = ~GPLEV0;              /* active low */
    unsigned mask = 0;
    for (unsigned i = 0; i < HAT_NUM_BUTTONS; i++)
        mask |= ((lev >> button_pins[i]) & 1u) << i;
    return mask;
}

void hat_led(int on)
{
    if (on) gpio_set(HAT_LED); else gpio_clr(HAT_LED);
}

void pi_act_led(int on)
{
    /* On the Zero 2 W the ACT LED is active low */
    if (on) gpio_clr(PI_ACT_LED); else gpio_set(PI_ACT_LED);
}

void hat_beep(unsigned hz, unsigned ms)
{
    if (hz == 0)
        return;
    uint32_t half = 500000u / hz;
    uint32_t n = (ms * 1000u) / (2 * half);
    while (n--) {
        gpio_set(HAT_BUZZER);
        delay_us(half);
        gpio_clr(HAT_BUZZER);
        delay_us(half);
    }
}
