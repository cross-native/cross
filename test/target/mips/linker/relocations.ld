/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* TEXT_START selects the image address; _gp lies 32 KiB into .sdata. */
SECTIONS
{
    . = TEXT_START;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    _gp = . + 0x7ff0;
    .sdata : { *(.sdata .sdata.*) }
    /DISCARD/ : { *(.reginfo) *(.MIPS.options) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) }
}
