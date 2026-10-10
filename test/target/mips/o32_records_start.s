# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

        .set noreorder
        .set noat
        .set mips3
        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $sp,0x8080
        addiu $sp,$sp,-64
        # Enable the FPU.
        mfc0 $t0,$12
        lui $t1,0x2000
        or $t0,$t0,$t1
        mtc0 $t0,$12
        nop
        nop
        nop
        nop
        jal o32_records_entry
        nop
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $v0,$v0,48
        sb $v0,0($t0)
        addiu $t1,$zero,10
        sb $t1,0($t0)
        move $a0,$zero
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
halt:   b halt
        nop
