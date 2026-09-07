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
        lui $a0,0x1234
        ori $a0,$a0,0x5600
        addiu $t0,$zero,0x78
        sw $t0,16($sp)
        jal never
        nop
.Lhalt:
        b .Lhalt
        nop

        .globl sink
sink:
        lui $t0,%hi(mips_noreturn_count)
        addiu $t0,$t0,%lo(mips_noreturn_count)
        lw $t1,0($t0)
        lui $t2,0x1234
        ori $t2,$t2,0x5678
        bne $a0,$t2,.Lfail
        nop
        addiu $t1,$t1,1
        sw $t1,0($t0)
        slti $t2,$t1,16
        bne $t2,$zero,.Lreturn
        nop
        addiu $a0,$zero,'1'
        b .Lreport
        nop
.Lfail:
        addiu $a0,$zero,'F'
.Lreport:
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        sb $a0,0($t0)
        addiu $a0,$zero,'\n'
        sb $a0,0($t0)
        move $a0,$zero
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
.Lreturn:
        move $a0,$zero
        move $a1,$zero
        move $a2,$zero
        move $a3,$zero
        move $v0,$zero
        move $v1,$zero
        jr $ra
        nop

        .section .bss,"aw",@nobits
        .p2align 2
mips_noreturn_count:
        .word 0
