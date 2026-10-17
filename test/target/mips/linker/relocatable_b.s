# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The second object of the relocatable-link test.

        .set    noreorder
        .text
        .globl  b_function
b_function:
        lui     $2, %hi(b_local)
        lw      $2, %lo(b_local)($2)
        jal     pick
        nop
        jr      $31
        nop

        .data
        .globl  b_data
b_data:
        .word   4, 5, 6
b_local:
        .word   7
        .word   b_function+4

        .section .text.pick,"axG",@progbits,pick,comdat
        .globl  pick
pick:
        jr      $31
        nop
