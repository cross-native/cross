/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* .data and .bss fill ram exactly when EXTRA is 0. */
MEMORY
{
    rom (rx) : ORIGIN = 0x80100000, LENGTH = 4K
    ram (rw) : ORIGIN = 0x80200000, LENGTH = 0x20
}

SECTIONS
{
    .text : { *(.text*) } > rom
    .rodata : { *(.rodata*) } > rom
    .data : { *(.data) } > ram AT> rom
    .bss (NOLOAD) : { *(.bss) . = . + EXTRA; } > ram
    /DISCARD/ : { *(*) }
}
