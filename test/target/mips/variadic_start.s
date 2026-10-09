# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Calls variadic_entry, then variadic_c_entry when it is linked, and prints P
# when each returns 0 with $sp intact, or F and the failing result in hex.

        .set noreorder
        .set noat
        .set mips3
        .weak variadic_c_entry
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
        move $s0,$sp
        jal variadic_entry
        nop
        bne $sp,$s0,fail
        nop
        bne $v0,$zero,fail
        nop
        lui $t0,%hi(variadic_c_entry)
        addiu $t0,$t0,%lo(variadic_c_entry)
        beq $t0,$zero,pass
        nop
        jalr $t0
        nop
        bne $sp,$s0,fail
        nop
        bne $v0,$zero,fail
        nop
pass:
        lui $s2,0xbf00
        ori $s2,$s2,0x0900
        addiu $t1,$zero,'P'
        b finish
        sb $t1,0($s2)
fail:
        move $s1,$v0
        lui $s2,0xbf00
        ori $s2,$s2,0x0900
        addiu $t1,$zero,'F'
        sb $t1,0($s2)
        addiu $s3,$zero,8
digit:
        srl $t1,$s1,28
        sll $s1,$s1,4
        sltiu $t2,$t1,10
        bne $t2,$zero,decimal
        addiu $t1,$t1,'0'
        addiu $t1,$t1,'a'-'0'-10
decimal:
        sb $t1,0($s2)
        addiu $s3,$s3,-1
        bne $s3,$zero,digit
        nop
finish:
        addiu $t1,$zero,10
        sb $t1,0($s2)
        move $a0,$zero
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
halt:
        b halt
        nop
