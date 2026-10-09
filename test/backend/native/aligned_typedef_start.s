# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
        .set noreorder
        .set noat
        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $29,0x8080
        addiu $29,$29,-64
        # Stable-to-private hard-float bridges may preserve o32 FPRs.
        mfc0 $8,$12
        lui $9,0x2000
        or $8,$8,$9
        mtc0 $8,$12
        nop
        nop
        move $16,$29
        jal aligned_typedef_entry
        nop
        bne $29,$16,fail
        nop
        addiu $8,$0,1
        bne $2,$8,fail
        nop
        addiu $9,$0,'P'
        b print
        nop
fail:
        addiu $9,$0,'F'
print:
        lui $8,0xbf00
        ori $8,$8,0x0900
        sb $9,0($8)
        addiu $9,$0,10
        sb $9,0($8)
        move $4,$0
        addiu $2,$0,1
        addiu $25,$0,1
        .word 0x7000007f
halt:
        b halt
        nop
