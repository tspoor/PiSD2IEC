// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// smp.h - start the other cores
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef SMP_H
#define SMP_H

/*
 * Start 'core' (1-3) with 'fn'. The core goes through smp_entry.S (own stack,
 * vectors, MMU and caches on) and then calls fn(). Returns 1 if the core
 * reports in within 100 ms, otherwise 0. fn() should not return; if it
 * does, the core parks itself.
 */
int smp_start_core(unsigned core, void (*fn)(void));

/* 1 if the core has started and reported in */
int smp_core_running(unsigned core);

#endif
