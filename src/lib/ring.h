// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// ring.h - lock-free queue between two cores (one writer, one reader)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
//
// Each queue has exactly one producer core and one consumer core. Then no
// locks are needed: the producer only writes 'head', the consumer only
// 'tail', and acquire/release makes sure a message only becomes visible
// once it has been written completely.
//
// ring_put() never waits: if the queue is full the message is lost and
// 'dropped' counts up. That way the IEC core can never hang on a slow reader.
//
// head and tail each sit on their own cache line (64 bytes), so the two
// cores don't keep taking over each other's line.
//
// Requires MMU and caches on for both cores (normal, inner shareable
// memory); mmu_init()/mmu_enable() take care of that.

#ifndef RING_H
#define RING_H

#include <stdint.h>

#define RING_SIZE 64u                   /* power of 2 */

struct msg {
    uint32_t type;
    uint32_t arg;
    uint64_t ticks;                     /* the producer's timer_ticks() */
};

struct ring {
    uint32_t   head __attribute__((aligned(64)));   /* producer only */
    uint32_t   dropped;                             /* producer only */
    uint32_t   tail __attribute__((aligned(64)));   /* consumer only */
    struct msg buf[RING_SIZE] __attribute__((aligned(64)));
};

static inline int ring_put(struct ring *r, uint32_t type, uint32_t arg, uint64_t ticks)
{
    uint32_t h = r->head;
    uint32_t t = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
    if (h - t >= RING_SIZE) {
        __atomic_store_n(&r->dropped, r->dropped + 1, __ATOMIC_RELAXED);
        return 0;
    }
    struct msg *m = &r->buf[h & (RING_SIZE - 1)];
    m->type  = type;
    m->arg   = arg;
    m->ticks = ticks;
    __atomic_store_n(&r->head, h + 1, __ATOMIC_RELEASE);
    return 1;
}

static inline int ring_get(struct ring *r, struct msg *out)
{
    uint32_t t = r->tail;
    uint32_t h = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
    if (t == h)
        return 0;
    *out = r->buf[t & (RING_SIZE - 1)];
    __atomic_store_n(&r->tail, t + 1, __ATOMIC_RELEASE);
    return 1;
}

/* Messages waiting? Read-only, so any core may ask (e.g. a third core) */
static inline int ring_pending(const struct ring *r)
{
    return __atomic_load_n(&r->head, __ATOMIC_ACQUIRE) !=
           __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
}

static inline uint32_t ring_dropped(const struct ring *r)
{
    return __atomic_load_n(&r->dropped, __ATOMIC_RELAXED);
}

#endif
