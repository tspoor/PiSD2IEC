# Third-party notices

PiSD2IEC contains or links the following third-party components. Their
notices are reproduced here as their licenses require, also for binary
distributions (`kernel8.img`).

## sd2iec

`src/sd2iec/` (except the `rpi/` layer) is sd2iec, unmodified; this includes
the files in `src/sd2iec/lpc17xx/`.

    sd2iec - SD/MMC to Commodore serial bus interface/controller
    Copyright (C) 2007-2026  Ingo Korb <ingo@akana.de>

    Inspired by MMC2IEC by Lars Pontoppidan et al.

    FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

Licensed under the GNU General Public License, version 2 only (see `COPYING`).
`src/sd2iec/rpi/progmem.h` is a verbatim copy of sd2iec's
`lpc17xx/progmem.h`. The files `rpi/arch-config.h`, `rpi/arch-eeprom.h`,
`rpi/autoconf.h`, `rpi/iec-bus.c`, `rpi/llfl-common.c` and `rpi/system.c`
are based on sd2iec
code and are therefore distributed under GPL version 2 only.

## FatFs

`src/sd2iec/ff.c` and `ff.h` (part of sd2iec):

    FatFs - FAT file system module  R0.06                     (C)ChaN, 2008
    Copyright (C) 2008, ChaN, all right reserved.

    * The FatFs module is a free software and there is no warranty.
    * You can use, modify and/or redistribute it for personal, non-profit or
    * commercial use without restriction under your responsibility.
    * Redistributions of source code must retain the above copyright notice.

## newlib

The firmware links a few C library functions from newlib, as shipped with the
Arm GNU Toolchain (`strchr`, `strrchr`, `strcpy`, `strncpy` and the `ctype`
table). They are covered by these newlib notices:

### Red Hat, Inc.

    Copyright (c) 1994-2009  Red Hat, Inc. All rights reserved.

    This copyrighted material is made available to anyone wishing to use,
    modify, copy, or redistribute it subject to the terms and conditions
    of the BSD License.   This program is distributed in the hope that
    it will be useful, but WITHOUT ANY WARRANTY expressed or implied,
    including the implied warranties of MERCHANTABILITY or FITNESS FOR
    A PARTICULAR PURPOSE.  A copy of this license is available at
    http://www.opensource.org/licenses. Any Red Hat trademarks that are
    incorporated in the source code or documentation are not subject to
    the BSD License and may only be used or replicated with the express
    permission of Red Hat, Inc.

### University of California, Berkeley

    Copyright (c) 1981-2000 The Regents of the University of California.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in the
          documentation and/or other materials provided with the distribution.
        * Neither the name of the University nor the names of its
          contributors may be used to endorse or promote products derived
          from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

## libgcc

`libgcc` is linked for compiler support routines. It is covered by the GCC
Runtime Library Exception, which allows it in programs under any license.

## 5x7 font

`src/lib/font5x7.c` contains glyphs converted from the X11 "misc-fixed" 5x7
font (X.Org font-misc-misc), which is in the public domain:
"Public domain font.  Share and enjoy."
