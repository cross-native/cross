/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80100000;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) *(.lit4 .lit8) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame) *(.pdr)
        *(.MIPS.abiflags) *(.MIPS.options) *(.reginfo) *(.gnu.attributes)
    }
}
