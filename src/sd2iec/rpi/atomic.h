// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// atomic.h - ATOMIC_BLOCK for sd2iec on the Pi
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// On AVR/LPC these macros disable interrupts. PiSD2IEC uses no interrupts:
// the 100 Hz tick runs on another core and only writes single 32-bit or
// 8-bit values, which a reader always sees whole. So a block is simply
// executed once.

#ifndef _UTIL_ATOMIC_H_
#define _UTIL_ATOMIC_H_ 1

#define ATOMIC_BLOCK(type)    for (type, __ToDo = 1; __ToDo; __ToDo = 0)
#define NONATOMIC_BLOCK(type) for (type, __ToDo = 1; __ToDo; __ToDo = 0)

#define ATOMIC_RESTORESTATE    unsigned int __sreg_save __attribute__((unused)) = 0
#define ATOMIC_FORCEON         unsigned int __sreg_save __attribute__((unused)) = 0
#define NONATOMIC_RESTORESTATE unsigned int __sreg_save __attribute__((unused)) = 0
#define NONATOMIC_FORCEOFF     unsigned int __sreg_save __attribute__((unused)) = 0

#endif
