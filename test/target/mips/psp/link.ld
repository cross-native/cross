/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x08804000;
    .text.psp.stub : { KEEP(*(.text.psp.stub)) } :text
    ASSERT(ADDR(.text.psp.stub) == 0x08804000,
           "PSP import stub address changed")
    ASSERT(SIZEOF(.text.psp.stub) >= 8,
           "PSP import stub must contain two instructions")
    .text : { *(.text.boot) *(.text .text.*) } :text
    ASSERT(. <= 0x08805000, "PSP test code overlaps module metadata")

    . = 0x08805000;
    .rodata.sceModuleInfo : { *metadata.bin(.data) } :data
    ASSERT(SIZEOF(.rodata.sceModuleInfo) == 0x5c,
           "PSP module metadata layout changed")
    .rodata : { *(.rodata .rodata.*) } :data
    .data : { *(.data .data.*) } :data
    .got : { *(.got .got.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) *(.MIPS.abiflags) *(.reginfo) }
}
