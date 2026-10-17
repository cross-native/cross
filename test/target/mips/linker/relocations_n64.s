# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# n64 RELA relocations: a 64-bit address built with R_MIPS_HIGHEST,
# R_MIPS_HIGHER, R_MIPS_HI16, and R_MIPS_LO16, R_MIPS_GPREL16, R_MIPS_26,
# R_MIPS_PC16, and R_MIPS_64 and R_MIPS_32 data.

        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        lui     $2, %highest(table+0x18000)
        daddiu  $2, $2, %higher(table+0x18000)
        dsll    $2, $2, 16
        daddiu  $2, $2, %hi(table+0x18000)
        dsll    $2, $2, 16
        daddiu  $2, $2, %lo(table+0x18000)
        lw      $4, %gp_rel(small)($28)
        jal     far_function
        nop
        b       elsewhere
        nop

        .data
        .globl  pointers
pointers:
        .dword  far_function+8
        .dword  table+0x10000
        .word   half_value
        .word   0
