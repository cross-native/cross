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
        jal count_advances
        nop
        jal print_digit
        move $a0,$v0
        jal single_forms
        nop
        jal print_digit
        move $a0,$v0
        jal double_forms
        nop
        jal print_digit
        move $a0,$v0
        jal compare_round_trip
        nop
        jal print_digit
        move $a0,$v0
        jal fcsr_round_trip
        nop
        jal print_digit
        move $a0,$v0
        jal tlb_round_trip
        nop
        jal print_digit
        move $a0,$v0
        jal cache_writeback
        nop
        jal print_digit
        move $a0,$v0
        jal nop_form
        nop
        jal print_digit
        move $a0,$v0
        # Naked functions: 1 + 42 and (2 << 2) + 1 come back in v0.
        addiu $a0,$zero,1
        jal naked_answer
        nop
        jal print_digit
        addiu $a0,$v0,-42
        addiu $a0,$zero,2
        jal naked_scaled
        nop
        jal print_digit
        addiu $a0,$v0,-8
        # ERET resumes at after_eret instead of returning here.
        lui $a0,%hi(after_eret)
        addiu $a0,$a0,%lo(after_eret)
        jal return_through_eret
        nop
        jal print_digit
        move $a0,$zero
after_eret:
        jal print_digit
        addiu $a0,$zero,1
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
