// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// string.c - memory functions that GCC also calls by itself
// (compiled with -fno-tree-loop-distribute-patterns, otherwise GCC turns
//  the loop in memset back into a call to memset)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    uint64_t v = (unsigned char)c * 0x0101010101010101ull;

    while (n && ((uintptr_t)d & 7)) { *d++ = (unsigned char)c; n--; }
    for (; n >= 8; n -= 8, d += 8)
        *(uint64_t *)d = v;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0) {
        for (; n >= 8; n -= 8, d += 8, s += 8)
            *(uint64_t *)d = *(const uint64_t *)s;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d <= s || d >= s + n)
        return memcpy(dst, src, n);
    d += n;
    s += n;
    while (n--) *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
