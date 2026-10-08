/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

OUTPUT_ARCH(mips)
ENTRY(_payload)

SECTIONS
{
    .payload 0xffffffff80100000 : { *(.payload) }
    /DISCARD/ : {
        *(.MIPS.abiflags) *(.MIPS.options) *(.reginfo)
        *(.text) *(.data) *(.bss) *(.comment)
        /* The wrapper contains only a byte-for-byte payload, with no GOT
           references. LLD's synthetic GOT would overlap the payload's
           unmaterialized BSS immediately after its last file-backed byte. */
        *(.got .got.*)
    }
}
