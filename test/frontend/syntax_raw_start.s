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
        mfc0 $8,$12
        lui $9,0x2000
        or $8,$8,$9
        mtc0 $8,$12
        nop
        nop
        # The harness explicitly selects the registered o32/n64 platform ABI.
        # This is its target-owned u32 result endpoint, not a compiler default.
        jal syntax_raw_entry
        nop
        addiu $8,$zero,61
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
