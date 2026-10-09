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
        # A hard-float o32 bridge may preserve FPRs even when the source uses
        # only integers. Bare startup owns enabling CP1 before entering it.
        mfc0 $8,$12
        lui $9,0x2000
        or $8,$8,$9
        mtc0 $8,$12
        nop
        nop
        jal forms_entry
        nop
        addiu $8,$zero,1
        bne $2,$8,fail
        addiu $4,$zero,80
        b print
        nop
fail:   addiu $4,$zero,70
print:  lui $8,0xbf00
        ori $8,$8,0x0900
        sb $4,0($8)
        addiu $4,$zero,10
        sb $4,0($8)
        move $4,$zero
        addiu $2,$zero,1
        addiu $25,$zero,1
        .word 0x7000007f
halt:   b halt
        nop
        # forms.x declares this o32 function at the fixed address 0x80100100,
        # 0x100 bytes after the image base of bare.ld.
        .org 0x100
fixed_add:
        jr $31
        addu $2,$4,$5
