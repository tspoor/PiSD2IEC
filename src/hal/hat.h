// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// hat.h - Pi1541 HAT option B (CBMretro vertical HAT), 40-pin header
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Option B has split IEC lines: each line has its own input (through a
// level shifter) and output (through a 7406 open collector).
// Pin numbers follow Pi1541 (src/iec_bus.h, HAS_40PINS).

#ifndef HAT_H
#define HAT_H

/* IEC inputs */
#define HAT_IN_ATN      24
#define HAT_IN_DATA     25
#define HAT_IN_CLOCK    26
#define HAT_IN_SRQ      21
#define HAT_IN_RESET    20

/* IEC outputs (to the 7406) */
#define HAT_OUT_ATN     12
#define HAT_OUT_CLOCK   17
#define HAT_OUT_DATA    18
#define HAT_OUT_SRQ     19

/*
 * Polarity. Option B: the 7406 inverts, so GPIO high = bus line low.
 * The inputs are not inverted: GPIO high = bus line high (released).
 * If a different HAT reads the bus the wrong way round, change it here.
 */
#define HAT_IEC_OUT_INVERTED  1
#define HAT_IEC_IN_INVERTED   0

/* Buttons (active low, internal pull-up) */
#define HAT_BUTTON1     27
#define HAT_BUTTON2     22
#define HAT_BUTTON3     23
#define HAT_BUTTON4     4
#define HAT_BUTTON5     5
#define HAT_NUM_BUTTONS 5

/* What the buttons do (button number - 1), as labelled on the Pi1541 HAT */
#define HAT_BTN_SELECT  0               /* GPIO 27 */
#define HAT_BTN_UP      1               /* GPIO 22 */
#define HAT_BTN_DOWN    2               /* GPIO 23 */
#define HAT_BTN_EXIT    3               /* GPIO 4  */
#define HAT_BTN_ADD     4               /* GPIO 5  */

#define HAT_LED         16
#define HAT_BUZZER      13

/* OLED: SSD1306 128x64 on I2C0 (BSC0), header pins 27/28. As in Pi1541:
   i2cBusMaster = 0, i2cLcdFlip = 1 (display mounted rotated 180 degrees). */
#define HAT_I2C_BUS     0
#define HAT_I2C_SDA     0
#define HAT_I2C_SCL     1
#define HAT_OLED_FLIP   1

/* ACT LED on the Pi Zero 2 W itself */
#define PI_ACT_LED      29

void     hat_init(void);
unsigned hat_buttons(void);              /* bit n = button n+1 pressed */
void     hat_led(int on);
void     pi_act_led(int on);
void     hat_beep(unsigned hz, unsigned ms);

#endif
