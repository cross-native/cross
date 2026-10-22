# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A bare-metal runtime that walks the ELF initializer tables: it calls each
# .init_array entry in order, then init_tables_entry, then each .fini_array
# entry from the last to the first, then init_tables_finished, and prints P
# when both return 0 with $sp intact, or F and the failing result in hex.

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
        move $s0,$sp
        lui $s4,%hi(__init_array_start)
        addiu $s4,$s4,%lo(__init_array_start)
        lui $s5,%hi(__init_array_end)
        addiu $s5,$s5,%lo(__init_array_end)
constructors:
        beq $s4,$s5,entry
        nop
        lw $t9,0($s4)
        jalr $t9
        addiu $s4,$s4,4
        b constructors
        nop
entry:
        jal init_tables_entry
        nop
        bne $sp,$s0,fail
        nop
        bne $v0,$zero,fail
        nop
        lui $s4,%hi(__fini_array_start)
        addiu $s4,$s4,%lo(__fini_array_start)
        lui $s5,%hi(__fini_array_end)
        addiu $s5,$s5,%lo(__fini_array_end)
destructors:
        beq $s5,$s4,finished
        nop
        lw $t9,-4($s5)
        jalr $t9
        addiu $s5,$s5,-4
        b destructors
        nop
finished:
        jal init_tables_finished
        nop
        bne $sp,$s0,fail
        nop
        bne $v0,$zero,fail
        nop
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
