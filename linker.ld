/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 T. Spoor
 *
 * linker.ld - memory layout of kernel8.img
 * Part of PiSD2IEC, a minimal sd2iec solution for the Pi.
 */

ENTRY(_start)

SECTIONS
{
    /* Typical load address for kernel8 on Raspberry Pi */
    . = 0x80000;

    /* Boot code first */
    .text.boot : ALIGN(16)
    {
        KEEP(*(.text.boot))
        KEEP(*(.text.boot.*))
    }

    /* Rest of text/rodata */
    .text : ALIGN(16)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    /* Initialized data */
    .data : ALIGN(16)
    {
        *(.data .data.*)
    }

    /* Uninitialized data */
    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);           /* start.S clears .bss 16 bytes at a time */
        __bss_end = .;
    }

    /* End-of-image symbol */
    __end = .;

    /* ---- Stacks (one per core) ----
     * Core n uses [__stack_start + n*SIZE, __stack_start + (n+1)*SIZE),
     * the stack grows down from the top. SIZE must match
     * CORE_STACK_SIZE in src/hal/smp_entry.S. */
    .stack (NOLOAD) : ALIGN(16)
    {
        __stack_start = .;
        . += 4 * 0x4000;         /* 4 cores x 16 KiB */
        __stack_end = .;
    }
    __stack_top = __stack_start + 0x4000;   /* core 0 (src/hal/start.S) */

    /* sd2iec "EEPROM": addresses only (offsets from 0), without contents.
     * The contents live in eeprom_ram[] (src/sd2iec/rpi/arch-eeprom.c). */
    .eeprom 0 (NOLOAD) :
    {
        *(.eeprom)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
    }
}

