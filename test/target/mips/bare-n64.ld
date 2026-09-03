/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* n64 objects are ELF64, so the image is linked directly at its
 * sign-extended KSEG0 address instead of being wrapped like the o32 test.
 */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0xffffffff80100000;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame)
        *(.MIPS.abiflags) *(.MIPS.options) *(.reginfo) *(.pdr)
    }
}
