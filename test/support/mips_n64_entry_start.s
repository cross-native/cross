# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The n64 counterpart of mips_entry_start.s: calls test_entry and prints P
# when it returns zero with a balanced stack, F otherwise, and T on any
# exception. Registers are numeric, as in n64 code $t0-$t3 name 12-15.
        .set noreorder
        .set noat
        .set mips64
        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $29,0x8080
        daddiu $29,$29,-64
        # Route the general exception vector (BEV clear) to trap_handler.
        lui $8,%hi(trap_handler)
        daddiu $8,$8,%lo(trap_handler)
        srl $8,$8,2
        lui $9,0x03ff
        ori $9,$9,0xffff
        and $8,$8,$9
        lui $9,0x0800
        or $8,$8,$9
        lui $10,0x8000
        sw $8,0x180($10)
        sw $0,0x184($10)
        # Clear BEV, ERL, EXL, and IE; enable the 64-bit FPU (CU1 and FR).
        mfc0 $8,$12
        lui $9,0xffbf
        ori $9,$9,0xfff8
        and $8,$8,$9
        lui $9,0x2400
        or $8,$8,$9
        mtc0 $8,$12
        nop
        nop
        move $16,$29
        jal test_entry
        nop
        bne $29,$16,fail
        nop
        bne $2,$0,fail
        nop
        daddiu $9,$0,'P'
        b print
        nop
fail:
        daddiu $9,$0,'F'
print:
        lui $8,0xbf00
        ori $8,$8,0x0900
        sb $9,0($8)
        daddiu $9,$0,10
        sb $9,0($8)
        move $4,$0
        daddiu $2,$0,1
        daddiu $25,$0,1
        # SDBBP 1, the Unified Hosting Interface trap.
        .word 0x7000007f
halt:
        b halt
        nop
trap_handler:
        daddiu $9,$0,'T'
        b print
        nop
