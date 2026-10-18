/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* abicalls.ld for n64 at sign-extended KSEG0 addresses. */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0xffffffff80100000;
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
