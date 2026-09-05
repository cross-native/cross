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
        addiu $a0,$zero,256
        jal bool_integer
        nop
        move $a0,$v0
        jal print_digit
        nop
        jal eval_width
        nop
        move $a0,$v0
        jal print_digit
        nop
        jal runtime_width
        nop
        move $a0,$v0
        jal print_digit
        nop
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $t1,$zero,10
        sb $t1,0($t0)
        move $a0,$zero
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
halt:   b halt
        nop
print_digit:
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $a0,$a0,48
        sb $a0,0($t0)
        jr $ra
        nop
