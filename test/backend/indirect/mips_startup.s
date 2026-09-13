# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
        .set noreorder
        .set noat
        .set mips3
        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $29,0x8080
        addiu $29,$29,-64
        # The hard-float target may legitimately preserve o32 FPRs around a
        # private Cross call. Bare startup owns enabling CP1.
        mfc0 $8,$12
        lui $9,0x2000
        or $8,$8,$9
        mtc0 $8,$12
        nop
        nop
        li $16,0x12345678
        addiu $4,$0,17
        jal indirect_test
        nop
        addiu $12,$0,470
        bne $2,$12,.Lfail
        nop
        addiu $4,$0,18
        jal indirect_test
        nop
        addiu $12,$0,1408
        bne $2,$12,.Lfail
        nop
        jal indirect_modes
        nop
        addiu $12,$0,51
        bne $2,$12,.Lfail
        nop
        jal indirect_wide
        nop
        addiu $12,$0,1
        bne $2,$12,.Lfail
        nop
        li $12,0x12345678
        bne $16,$12,.Lfail
        nop
        addiu $4,$0,'P'
        b .Lreport
        nop
.Lfail:
        addiu $4,$0,'F'
.Lreport:
        lui $12,0xbf00
        ori $12,$12,0x0900
        sb $4,0($12)
        addiu $4,$0,'\n'
        sb $4,0($12)
        move $4,$0
        addiu $2,$0,1
        addiu $25,$0,1
        .word 0x7000007f
.Lhalt:
        b .Lhalt
        nop
