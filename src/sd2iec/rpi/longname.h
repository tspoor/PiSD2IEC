// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 T. Spoor
//
// longname.h - full long file names of a FAT directory (see longname.c)
// Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

#ifndef LONGNAME_H
#define LONGNAME_H

#include <stdint.h>
#include "ff.h"

#define LONGNAME_MAX 64                 /* characters kept per name */

/*
 * Walk the FAT directory that starts at dir_cluster (0 = root) and call
 * found() for every file and directory in directory order: sfn is the raw
 * 11-byte 8.3 name of the entry, lfn its long name in ASCII (other
 * characters as '?', cut at LONGNAME_MAX) or NULL if it has none.
 * Core 1 only. Returns 0, or -1 on a read error or FAT12.
 */
int longname_scan(FATFS *fs, uint32_t dir_cluster,
                  void (*found)(const uint8_t *sfn, const char *lfn, void *ctx),
                  void *ctx);

#endif
