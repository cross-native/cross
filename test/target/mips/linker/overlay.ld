/* Copyright (C) 2026 Cross contributors
   SPDX-License-Identifier: GPL-3.0-or-later

   OVERLAY sections at one run address, loaded one after another from an
   explicit load address, with a fill; then an OVERLAY in MEMORY regions. */

MEMORY {
  rom (rx) : ORIGIN = 0x80100000, LENGTH = 64K
  ram (!rx) : ORIGIN = 0x80200000, LENGTH = 64K
}
ENTRY(_start)
SECTIONS {
  .text : { *(.text) } > rom
  OVERLAY 0x80300000 : NOCROSSREFS AT (0x80108000) {
    .ova { *(.ov_a) }
    .ovb { *(.ov_b) . = ALIGN(32); } =0x11223344
  }
  after_overlay = .;
  OVERLAY : {
    .ovc { *(.ov_c) }
  } > ram AT > rom
  .data : { *(.data) } > ram AT > rom
  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }
}
