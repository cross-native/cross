# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# o32 REL relocations: two R_MIPS_HI16 sharing one R_MIPS_LO16 with a
# carry into the high half, R_MIPS_GPREL16 against a global and a local,
# R_MIPS_26 and R_MIPS_PC16 into another object, R_MIPS_32, R_MIPS_16.

        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        lui     $2, %hi(table+0x8010)
        lui     $3, %hi(table+0x8010)
        addiu   $2, $2, %lo(table+0x8010)
        lw      $4, %gp_rel(small)($28)
        lw      $5, %gp_rel(local_small+4)($28)
        jal     far_function
        nop
        b       elsewhere
        nop

        .data
        .globl  pointers
pointers:
        .word   far_function+4
        .word   table+0x10000

        # GNU ld applies R_MIPS_16 to the low half of a word.
        .section .rodata.half,"a",@progbits
        .2byte  half_value

        .sdata
local_small:
        .word   1
        .word   2
