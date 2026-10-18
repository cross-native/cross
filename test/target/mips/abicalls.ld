/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* bare.ld with the sections of abicalls and thread-local code: the TLS
 * template, whose parts abicalls_start.s copies into the thread's block,
 * and the GOT, with _gp 32 KiB past its start.
 */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80100000;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .tdata : { *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    __tls_data = ADDR(.tdata);
    __tls_data_size = SIZEOF(.tdata);
    __tls_zero = ADDR(.tbss);
    __tls_zero_size = SIZEOF(.tbss);
    . = ALIGN(16);
    _gp = . + 0x7ff0;
    .got : { *(.got) }
    .sdata : { *(.sdata .sdata.*) }
    .sbss (NOLOAD) : { *(.sbss .sbss.*) *(.scommon) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame) *(.pdr) *(.gnu.attributes)
        *(.MIPS.abiflags) *(.MIPS.options) *(.reginfo) *(.mdebug.*)
    }
}
