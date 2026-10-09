# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Calls wide_across of preserved_width.x with a canary in s0-s7 and fp, then
# checks its result and the canaries. narrow_callee saves and restores s0-s7
# with sw and lw, as a MIPS I callee does, so on this 64-bit CPU each
# restored register holds only its sign-extended low word. Prints P on
# success or F on failure. The expected value comes from an independent C
# reference.

        .set noreorder
        .set noat
        .set mips3

        .macro LOAD32 reg, value
        lui \reg, ((\value) >> 16) & 0xffff
        ori \reg, \reg, (\value) & 0xffff
        .endm

        .macro CHECK reg, value
        LOAD32 $t0, \value
        bne \reg, $t0, fail
        nop
        .endm

        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $sp, 0x8080
        addiu $sp, $sp, -64
        LOAD32 $s0, 0x8a5a0110
        LOAD32 $s1, 0x8a5a0221
        LOAD32 $s2, 0x8a5a0332
        LOAD32 $s3, 0x8a5a0443
        LOAD32 $s4, 0x8a5a0554
        LOAD32 $s5, 0x8a5a0665
        LOAD32 $s6, 0x8a5a0776
        LOAD32 $s7, 0x8a5a0887
        LOAD32 $fp, 0x8a5a0998
        li $a0, 45
        jal wide_across
        nop
        sll $v0, $v0, 0
        CHECK $v0, 0x58888879
        CHECK $s0, 0x8a5a0110
        CHECK $s1, 0x8a5a0221
        CHECK $s2, 0x8a5a0332
        CHECK $s3, 0x8a5a0443
        CHECK $s4, 0x8a5a0554
        CHECK $s5, 0x8a5a0665
        CHECK $s6, 0x8a5a0776
        CHECK $s7, 0x8a5a0887
        CHECK $fp, 0x8a5a0998
        li $t1, 80
        b report
        nop
fail:
        li $t1, 70
report:
        LOAD32 $t0, 0xbf000900
        sb $t1, 0($t0)
        li $t1, 10
        sb $t1, 0($t0)
        move $a0, $zero
        addiu $v0, $zero, 1
        addiu $t9, $zero, 1
        .word 0x7000007f
halt:
        b halt
        nop

        .globl narrow_callee
# u32 narrow_callee(u32 value) returns 2 * value + 3.
narrow_callee:
        addiu $sp, $sp, -32
        sw $s0, 0($sp)
        sw $s1, 4($sp)
        sw $s2, 8($sp)
        sw $s3, 12($sp)
        sw $s4, 16($sp)
        sw $s5, 20($sp)
        sw $s6, 24($sp)
        sw $s7, 28($sp)
        LOAD32 $at, 0x5a5a5a5a
        move $s0, $at
        move $s1, $at
        move $s2, $at
        move $s3, $at
        move $s4, $at
        move $s5, $at
        move $s6, $at
        move $s7, $at
        sll $v0, $a0, 1
        addiu $v0, $v0, 3
        lw $s0, 0($sp)
        lw $s1, 4($sp)
        lw $s2, 8($sp)
        lw $s3, 12($sp)
        lw $s4, 16($sp)
        lw $s5, 20($sp)
        lw $s6, 24($sp)
        lw $s7, 28($sp)
        jr $ra
        addiu $sp, $sp, 32
