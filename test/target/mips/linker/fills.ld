/* Copyright (C) 2026 Cross contributors
   SPDX-License-Identifier: GPL-3.0-or-later

   Fill patterns (a long hexadecimal one and FILL), SUBALIGN, ONLY_IF_RO
   and ONLY_IF_RW, output section types, and symbols assigned inside and
   outside output sections. */

ENTRY(_start)
SECTIONS {
  . = 0x80100000;
  .text : { *(.text) . = . + 3; FILL(0xaabbccdd) *(.text.b) text_offset = 0x10; } =0x0102030405
  top_of_text = .;
  .sub : SUBALIGN(32) { *(.a4) *(.a64) }
  .stuff : ONLY_IF_RO { *(.stuff) }
  .stuff : ONLY_IF_RW { stuff_start = .; *(.stuff) }
  .ro (READONLY) : { *(.ro) }
  .info (INFO) : { *(.info) }
  .typed (TYPE = SHT_PROGBITS) : { BYTE(1) }
  . = ALIGN(16);
  after_align = .;
  .data : { *(.data) b_size = end_b - b; }
  .bss : { *(.bss) }
  _end = .;
  past_end = .;
  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }
}
