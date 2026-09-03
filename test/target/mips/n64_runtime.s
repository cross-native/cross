# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# n64 startup for qemu-system-mips64 -M malta -cpu MIPS64R2-generic.
#
# GPRs are spelled numerically: the n64 assembler reads $t0-$t3 as the
# architectural registers 12-15, so only numbers are unambiguous between the
# hand-written startup and the Cross output it calls.  Legend:
#   $0 zero   $2/$3 results   $4-$11 arguments a0-a7
#   $12-$15 temporaries   $24 scratch   $26 failure code
#   $29 sp    $31 ra
#
# Addresses stay in sign-extended KSEG0/KSEG1, which is exactly the sym32
# code model the backend emits with lui %hi + daddiu %lo.

        .set    noreorder
        .set    noat
        .set    mips64

        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64             # 16-byte aligned n64 stack

        # QEMU enters with CP1 disabled and FR clear.  n64 is defined on a
        # 64-bit FPU, so enable CU1 (0x20000000) and FR (0x04000000).
        mfc0    $12,$12
        lui     $13,0x2400
        or      $12,$12,$13
        mtc0    $12,$12
        nop
        nop

        # n64_arith(0x80000000, -3, 0x0000000100000000)
        #   = 0x100000000 + 0x80000007 + (-8) = 0x17fffffff
        daddiu  $26,$0,'1'
        lui     $4,0x8000
        daddiu  $5,$0,-3
        dli     $6,0x0000000100000000
        jal     n64_arith
        nop
        dli     $12,0x000000017fffffff
        bne     $2,$12,.Lfail
        nop

        # n64_sum_indexed(n64_table, 5) sums five doublewords.
        daddiu  $26,$0,'2'
        lui     $4,%hi(n64_table)
        daddiu  $4,$4,%lo(n64_table)
        daddiu  $5,$0,5
        jal     n64_sum_indexed
        nop
        dli     $12,0x0000000f0000000f
        bne     $2,$12,.Lfail
        nop

        # A global object behind a global pointer: both the pointer load and
        # the object load are 8 bytes wide.
        daddiu  $26,$0,'3'
        dli     $4,0xffffffffffffffff
        jal     n64_globals
        nop
        dli     $12,0xffffffffffffffff
        bne     $2,$12,.Lfail
        nop
        lui     $12,%hi(n64_sink)
        daddiu  $12,$12,%lo(n64_sink)
        ld      $13,0($12)
        dli     $14,0xfedcba9876543210
        bne     $13,$14,.Lfail
        nop

        # n64_mixed_floats(3, 2.5f64, 0.5f32): the shared argument cursor
        # puts the f64 in $f13 and the f32 in $f14, not in $5/$6.
        daddiu  $26,$0,'4'
        daddiu  $4,$0,3
        dli     $12,0x4004000000000000  # 2.5f64
        dmtc1   $12,$f13
        lui     $12,0x3f00              # 0.5f32
        mtc1    $12,$f14
        jal     n64_mixed_floats
        nop
        dmfc1   $12,$f0
        dli     $13,0x402b000000000000  # 13.5f64
        bne     $12,$13,.Lfail
        nop

        # Ten arguments: a0-a7 in $4-$11, a8/a9 in the caller's packed
        # 8-byte slots at 0($sp) and 8($sp).  n64 has no home area.
        daddiu  $26,$0,'5'
        daddiu  $4,$0,1
        daddiu  $5,$0,2
        daddiu  $6,$0,3
        daddiu  $7,$0,4
        daddiu  $8,$0,5
        daddiu  $9,$0,6
        daddiu  $10,$0,7
        daddiu  $11,$0,8
        daddiu  $12,$0,9
        sd      $12,0($29)
        daddiu  $12,$0,10
        sd      $12,8($29)
        jal     n64_many_arguments
        nop
        daddiu  $12,$0,385
        bne     $2,$12,.Lfail
        nop

        # A call chain: 13*seed + 105 with $ra and one callee-saved register
        # travelling through sd/ld.
        daddiu  $26,$0,'6'
        daddiu  $4,$0,7
        jal     n64_call_chain
        nop
        daddiu  $12,$0,196
        bne     $2,$12,.Lfail
        nop

        # A pointer live across a call, so it is spilled and reloaded whole.
        daddiu  $26,$0,'7'
        lui     $4,%hi(n64_cells)
        daddiu  $4,$4,%lo(n64_cells)
        dli     $12,0x1000
        sd      $12,8($4)
        daddiu  $5,$0,5
        jal     n64_pointer_across_call
        nop
        dli     $12,0x1010
        bne     $2,$12,.Lfail
        nop
        lui     $12,%hi(n64_cells)
        daddiu  $12,$12,%lo(n64_cells)
        ld      $13,0($12)
        daddiu  $14,$0,16
        bne     $13,$14,.Lfail
        nop

        # An out parameter travels as a 64-bit pointer in $5.
        daddiu  $26,$0,'8'
        daddiu  $4,$0,6
        lui     $5,%hi(n64_cells)
        daddiu  $5,$5,%lo(n64_cells)
        jal     n64_out_parameter
        nop
        lui     $12,%hi(n64_cells)
        daddiu  $12,$12,%lo(n64_cells)
        ld      $13,0($12)
        daddiu  $14,$0,33
        bne     $13,$14,.Lfail
        nop

        # A surviving private definition still receives a whole 64-bit
        # pointer; a 32-bit private contract would truncate it.
        daddiu  $26,$0,'9'
        lui     $4,%hi(n64_cells)
        daddiu  $4,$4,%lo(n64_cells)
        dli     $12,0x1111111111111111
        sd      $12,0($4)
        dli     $12,0x2222222222222222
        sd      $12,8($4)
        dli     $5,0x1000
        jal     n64_private_entry
        nop
        dli     $12,0x6666666666668666
        bne     $2,$12,.Lfail
        nop

        daddiu  $4,$0,'P'
        b       .Lreport
        nop
.Lfail:
        move    $4,$26
.Lreport:
        xori    $14,$4,'P'
        sltu    $14,$0,$14
        # Malta's first 16550 UART through sign-extended KSEG1.
        lui     $12,0xbf00
        ori     $12,$12,0x0900
        sb      $4,0($12)
        daddiu  $4,$0,'\n'
        sb      $4,0($12)
        move    $4,$14
        daddiu  $2,$0,1
        daddiu  $25,$0,1
        # SDBBP 1, the Unified Hosting Interface trap.
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop
        .end    _start

        .section .data,"aw",@progbits
        .p2align 3
n64_table:
        .quad   0x0000000100000001
        .quad   0x0000000200000002
        .quad   0x0000000300000003
        .quad   0x0000000400000004
        .quad   0x0000000500000005

        .section .bss,"aw",@nobits
        .p2align 3
n64_cells:
        .space  32
