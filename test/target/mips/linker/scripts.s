# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Input of the linker-script test: an entry, a used, an unused, and a kept
# function in their own sections, data, an orphan, zero-filled data, and a
# discarded comment.

        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        jal     used
        nop
        lui     $2, %hi(provided)
        addiu   $2, $2, %lo(provided)
        lui     $3, %hi(_binary_blob_bin_start)
        addiu   $3, $3, %lo(_binary_blob_bin_start)
        lui     $4, %hi(constant)
        lw      $4, %lo(constant)($4)
        lui     $5, %hi(orphan_data)
        lw      $5, %lo(orphan_data)($5)
        lui     $6, %hi(variable)
        lw      $6, %lo(variable)($6)
        lui     $7, %hi(buffer)
        sw      $0, %lo(buffer)($7)

        .section .text.used,"ax",@progbits
        .globl  used
used:
        jr      $31
        nop

        .section .text.unused,"ax",@progbits
        .globl  unused
unused:
        jr      $31
        nop

        .section .text.kept,"ax",@progbits
kept:
        jr      $31
        nop

        .section .rodata,"a",@progbits
constant:
        .word   1

        .section .rodata.orphan,"a",@progbits
orphan_data:
        .word   2

        .data
variable:
        .word   3

        .bss
buffer:
        .space  16

        .section .comment,"MS",@progbits,1
        .asciz  "discarded"
