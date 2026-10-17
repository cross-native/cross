# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Subsections: a section's subsections follow in number order, `.org` counts
# from the section's start, `.previous` and `.popsection` return to the
# subsection they left, and label differences span subsections. The .table
# bytes are a0 a1 10 11 20 21 30 31 0a 04.
        .set noreorder
        .section .table,"a"
start:
        .byte   0xa0
        .pushsection .table, 3
        .org    6
        .byte   0x30, 0x31
        .popsection
        .pushsection .table, 1
        .org    2
        .byte   0x10, 0x11
        .popsection
        .subsection 2
middle:
        .byte   0x20, 0x21
        .previous
        .byte   0xa1
        .subsection 4
        .byte   end - start, middle - start
end:
        .text 1
later:
        nop
        .text 0
        jr      $31
        nop
        .data
        .word   later
