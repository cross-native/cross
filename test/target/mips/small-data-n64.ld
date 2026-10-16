/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* bare-n64.ld with small data, as in small-data.ld. */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0xffffffff80100000;
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
