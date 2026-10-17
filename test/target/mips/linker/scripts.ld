/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

ENTRY(_start)
INCLUDE scripts_part.ld

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

PROVIDE(provided = 0x12345678);
PROVIDE(unused_provided = 1);

SECTIONS
{
    . = 0x80100000;
    .text : {
        KEEP(*(.text.boot))
        *(.text.used)
        KEEP(*(.text.kept))
        *(.text.unused)
    } :text
    text_size = SIZEOF(.text);
    .rodata : { *(.rodata) LONG(0xdeadbeef) BYTE(7) }
    . = ALIGN(0x100);
    .data : AT(0x80200000) {
        data_start = .;
        *(.data)
        *blob.bin(.data)
    } :data
    data_load = LOADADDR(.data);
    .bss (NOLOAD) : { *(.bss) *(COMMON) }
    _end = .;
    ASSERT(_end <= 0x80110000, "image too large")
    /DISCARD/ : { *(.comment) *(.reginfo) *(.MIPS.abiflags) *(.pdr) }
}
