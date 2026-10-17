# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The first object of the relocatable-link test: calls and %hi/%lo pairs
# into the other object, local references that become section-symbol
# relocations, a common symbol, and a COMDAT group.

        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        jal     b_function
        nop
        lui     $2, %hi(b_data+8)
        lw      $2, %lo(b_data+8)($2)
        lui     $3, %hi(a_local+4)
        lw      $3, %lo(a_local+4)($3)
        lui     $4, %hi(shared)
        lw      $4, %lo(shared)($4)
        j       a_tail
        nop
a_tail:
        jr      $31
        nop

        .data
        .word   1
a_local:
        .word   2, 3
        .word   a_tail

        .comm   shared, 16, 8

        .section .text.pick,"axG",@progbits,pick,comdat
        .globl  pick
pick:
        jr      $31
        nop
