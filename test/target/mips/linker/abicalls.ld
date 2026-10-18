/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* TEXT_START selects the image address; the TLS sections follow the data,
 * and _gp lies 32 KiB into the GOT.
 */
SECTIONS
{
    . = TEXT_START;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .tdata : { *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    . = ALIGN(16);
    _gp = . + 0x7ff0;
    .got : { *(.got) }
    /DISCARD/ : { *(.reginfo) *(.MIPS.options) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) }
}
