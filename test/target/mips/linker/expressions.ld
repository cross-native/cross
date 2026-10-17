/* Copyright (C) 2026 Cross contributors
   SPDX-License-Identifier: GPL-3.0-or-later

   Empty output sections and expression values inside output sections. */

ENTRY(_start)
SECTIONS {
  . = 0x80100000;
  .text : { *(.text) }
  /* Kept although empty: a KEEP input section, assignments to `.`. */
  .kept : { KEEP(*(.empty)) }
  .pad : { . = ALIGN(16); }
  .moved : { . = ALIGN(4) + 0; }
  /* Left out: assignments to `.` that change nothing, an empty input
     section, and symbol assignments alone. */
  .plus : { . = . + 0; }
  .cond : { . = ALIGN(. != 0 ? 64 : 1); }
  .loose : { *(.empty2) }
  .marks : { mark = .; }
  /* MAX, two-argument ALIGN, and comparisons see relative values as
     offsets in their section; a plain number is an offset from the start
     of the section it is assigned in. */
  .data 0x80200004 : {
    *(.data)
    . = MAX(., 0x12);
    data_end = .;
    aligned = ALIGN(., 8);
    offset = 0x20;
    distance = data_end - _start;
  }
  .lbss : { *(.lbss) . = ALIGN(. != 0 ? 8 : 1); }
  .bss : { *(.bss) . = ALIGN(. != 0 ? 8 : 1); }
  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }
}
