/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* bare.ld with small data: .sdata and .sbss follow each other, and _gp
 * lies 32 KiB past their start, so a $gp holding _gp reaches both with
 * 16-bit offsets.
 */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80100000;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    _gp = . + 0x7ff0;
    .sdata : { *(.sdata .sdata.*) }
    .sbss (NOLOAD) : { *(.sbss .sbss.*) *(.scommon) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame) *(.pdr) *(.gnu.attributes)
        *(.MIPS.abiflags) *(.MIPS.options) *(.reginfo) *(.mdebug.*)
    }
}
