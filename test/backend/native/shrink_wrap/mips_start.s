# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Calls each shrink-wrapping kernel with a canary in every o32 preserved
# register, then checks its result and the canaries. Prints P on success, or
# F and a letter naming the failed check. Expected values come from a C
# reference of the same kernels.

        .set noreorder
        .set noat
        .set mips3

        .macro LOAD32 reg, value
        lui \reg, ((\value) >> 16) & 0xffff
        ori \reg, \reg, (\value) & 0xffff
        .endm

        .macro CHECK function, expected
        addiu $k1, $k1, 1
        jal set_canaries
        nop
        jal \function
        nop
        LOAD32 $t0, \expected
        bne $v0, $t0, fail
        nop
        jal check_canaries
        nop
        .endm

        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $sp, 0x8080
        addiu $sp, $sp, -64
        move $k1, $zero
        jal sw_tree_values
        nop
        move $k0, $v0

        li $a0, 0
        CHECK sw_fib, 0
        li $a0, 1
        CHECK sw_fib, 1
        li $a0, 15
        CHECK sw_fib, 0x262
        li $a0, 20
        CHECK sw_fib, 0x1a6d

        li $a0, 1
        li $a1, 5
        li $a2, 9
        CHECK sw_tak, 9
        li $a0, 18
        li $a1, 12
        li $a2, 6
        CHECK sw_tak, 7
        li $a0, 12
        li $a1, 8
        li $a2, 4
        CHECK sw_tak, 5

        move $a0, $k0
        li $a1, 63
        li $a2, 0
        CHECK sw_tree, 0xdaedd1e4
        move $a0, $k0
        li $a1, 63
        li $a2, 70
        CHECK sw_tree, 0
        move $a0, $k0
        li $a1, 63
        li $a2, 5
        CHECK sw_tree, 0x7c6ad6c8

        li $a0, 0
        li $a1, 9
        CHECK sw_multi, 9
        li $a0, 5
        li $a1, 0
        CHECK sw_multi, 6
        li $a0, 3
        li $a1, 8
        CHECK sw_multi, 0x1b
        li $a0, 9
        li $a1, 4
        CHECK sw_multi, 0x6bf3c600
        li $a0, 100
        li $a1, 7
        CHECK sw_multi, 0x9e377d9d

        li $a0, 0
        li $a1, 77
        CHECK sw_loop, 0x4d
        li $a0, 20
        li $a1, 5
        CHECK sw_loop, 0x3efbf3d5

        li $a0, 0
        li $a1, 99
        CHECK sw_lanes, 0x63
        li $a0, 7
        li $a1, 12345
        CHECK sw_lanes, 0x15e446cf

        li $a0, 6
        CHECK sw_one_path, 0x2a
        li $a0, 7
        CHECK sw_one_path, 0x9e377b47

        li $a0, 3
        CHECK sw_noreturn, 4
        li $a0, 500
        CHECK sw_noreturn, 0x9e3877df

        # The last path calls sw_fatal, which reports the result.
        addiu $k1, $k1, 1
        jal set_canaries
        nop
        li $a0, 5000
        jal sw_noreturn
        nop
        b fail
        nop

        .globl sw_fatal
sw_fatal:
        andi $t0, $sp, 7
        bne $t0, $zero, fail
        nop
        li $t0, 0x49d2
        bne $a0, $t0, fail
        nop
        LOAD32 $t0, 0xbf000900
        li $t1, 80
        sb $t1, 0($t0)
        b report
        nop
fail:
        LOAD32 $t0, 0xbf000900
        li $t1, 70
        sb $t1, 0($t0)
        addiu $t1, $k1, 64
        sb $t1, 0($t0)
report:
        li $t1, 10
        sb $t1, 0($t0)
        move $a0, $zero
        addiu $v0, $zero, 1
        addiu $t9, $zero, 1
        .word 0x7000007f
halt:
        b halt
        nop

set_canaries:
        LOAD32 $s0, 0x8a5a0110
        LOAD32 $s1, 0x8a5a0221
        LOAD32 $s2, 0x8a5a0332
        LOAD32 $s3, 0x8a5a0443
        LOAD32 $s4, 0x8a5a0554
        LOAD32 $s5, 0x8a5a0665
        LOAD32 $s6, 0x8a5a0776
        LOAD32 $s7, 0x8a5a0887
        LOAD32 $fp, 0x8a5a0998
        jr $ra
        nop

check_canaries:
        LOAD32 $t0, 0x8a5a0110
        bne $s0, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0221
        bne $s1, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0332
        bne $s2, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0443
        bne $s3, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0554
        bne $s4, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0665
        bne $s5, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0776
        bne $s6, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0887
        bne $s7, $t0, fail
        nop
        LOAD32 $t0, 0x8a5a0998
        bne $fp, $t0, fail
        nop
        jr $ra
        nop
