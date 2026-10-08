# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Startup for the 64-bit-address Cross ABI under
# qemu-system-mips64 -M malta -cpu MIPS64R2-generic.
#
# Tests 1-7 call the Cross definitions of cross_n64_callee.x directly, with
# every argument placed as the cross-n64 model assigns it: integers in
# a0-a3 ($4-$7), t0-t9 ($8-$15, $24, $25), then s0-s7 ($16-$23); floating
# values in f12, f14, f16, f18, f4, f6, f8, f10; then packed 8-byte stack
# slots from 0($sp).  Results use v0/v1 ($2/$3) and f0.  Cross preserves
# s0-s7 and the frame pointer $30.  Tests 8-D call the n64 entry points of
# the separately compiled cross_n64_caller.x, which must also preserve gp
# and f24-f31 around their Cross calls.  The expected values come from an
# independent C reference.
#
# GPRs are numeric because the n64 assembler reads $t0-$t3 as 12-15.
# $26 holds the current failure code.

        .set    noreorder
        .set    noat
        .set    mips64

        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64

        # QEMU enters with CP1 disabled and FR clear; the 64-bit address
        # model uses the 64-bit FPU, so enable CU1 and FR.
        mfc0    $12,$12
        lui     $13,0x2400
        or      $12,$12,$13
        mtc0    $12,$12
        nop
        nop

        # 1: twenty-four integers; x14-x21 travel in s0-s7 and x22/x23 in
        # the caller's stack slots.  The sum is 4900 * 0x100000001.
        daddiu  $26,$0,'1'
        dli     $1,0x0000001700000017
        sd      $1,0($29)
        dli     $1,0x0000001800000018
        sd      $1,8($29)
        dli     $4,0x0000000100000001
        dli     $5,0x0000000200000002
        dli     $6,0x0000000300000003
        dli     $7,0x0000000400000004
        dli     $8,0x0000000500000005
        dli     $9,0x0000000600000006
        dli     $10,0x0000000700000007
        dli     $11,0x0000000800000008
        dli     $12,0x0000000900000009
        dli     $13,0x0000000a0000000a
        dli     $14,0x0000000b0000000b
        dli     $15,0x0000000c0000000c
        dli     $24,0x0000000d0000000d
        dli     $25,0x0000000e0000000e
        dli     $16,0x0000000f0000000f
        dli     $17,0x0000001000000010
        dli     $18,0x0000001100000011
        dli     $19,0x0000001200000012
        dli     $20,0x0000001300000013
        dli     $21,0x0000001400000014
        dli     $22,0x0000001500000015
        dli     $23,0x0000001600000016
        dli     $30,0x5e5e5e5e0000001e
        jal     cross_n64_weighted
        nop
        dli     $1,0x0000132400001324
        bne     $2,$1,.Lfail
        nop
        # s-register arguments are preserved for the caller.
        dli     $1,0x0000000f0000000f
        bne     $16,$1,.Lfail
        nop
        dli     $1,0x0000001000000010
        bne     $17,$1,.Lfail
        nop
        dli     $1,0x0000001100000011
        bne     $18,$1,.Lfail
        nop
        dli     $1,0x0000001200000012
        bne     $19,$1,.Lfail
        nop
        dli     $1,0x0000001300000013
        bne     $20,$1,.Lfail
        nop
        dli     $1,0x0000001400000014
        bne     $21,$1,.Lfail
        nop
        dli     $1,0x0000001500000015
        bne     $22,$1,.Lfail
        nop
        dli     $1,0x0000001600000016
        bne     $23,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e0000001e
        bne     $30,$1,.Lfail
        nop

        # 2: cross_n64_mixed(3, 2.5, 0.5f32, 100) = 113.5.  Independent
        # cursors place the doubles in f12/f14 and the integers in a0/a1;
        # decoys sit where a shared n64 cursor would look.
        daddiu  $26,$0,'2'
        jal     set_sentinels
        nop
        daddiu  $4,$0,3
        daddiu  $5,$0,100
        dli     $1,0x4004000000000000
        dmtc1   $1,$f12
        lui     $1,0x3f00
        mtc1    $1,$f14
        dli     $1,0x408f400000000000
        dmtc1   $1,$f13
        dmtc1   $1,$f15
        daddiu  $6,$0,77
        daddiu  $7,$0,77
        jal     cross_n64_mixed
        nop
        dmfc1   $24,$f0
        dli     $1,0x405c600000000000
        bne     $24,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 3: nine doubles i + 0.5; the ninth is in the first stack slot.
        # The weighted sum is 262.5.
        daddiu  $26,$0,'3'
        jal     set_sentinels
        nop
        dli     $1,0x4021000000000000
        sd      $1,0($29)
        dli     $1,0x3fe0000000000000
        dmtc1   $1,$f12
        dli     $1,0x3ff8000000000000
        dmtc1   $1,$f14
        dli     $1,0x4004000000000000
        dmtc1   $1,$f16
        dli     $1,0x400c000000000000
        dmtc1   $1,$f18
        dli     $1,0x4012000000000000
        dmtc1   $1,$f4
        dli     $1,0x4016000000000000
        dmtc1   $1,$f6
        dli     $1,0x401a000000000000
        dmtc1   $1,$f8
        dli     $1,0x401e000000000000
        dmtc1   $1,$f10
        jal     cross_n64_float_weighted
        nop
        dmfc1   $24,$f0
        dli     $1,0x4070680000000000
        bne     $24,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 4: a two-doubleword record in a0/a1 and bias in a2; the result
        # record returns in v0/v1.
        daddiu  $26,$0,'4'
        jal     set_sentinels
        nop
        dli     $4,0x0123456789abcdef
        dli     $5,0x0fedcba987654321
        daddiu  $6,$0,0x1000
        jal     cross_n64_swap
        nop
        dli     $1,0x0fedcba987655321
        bne     $2,$1,.Lfail
        nop
        dli     $1,0x0123456789abddef
        bne     $3,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 5: a five-doubleword result through a hidden pointer in a0; the
        # seed 0x100000000 shifts to a1.
        daddiu  $26,$0,'5'
        jal     set_sentinels
        nop
        lui     $4,%hi(cross_n64_spread_area)
        daddiu  $4,$4,%lo(cross_n64_spread_area)
        dli     $5,0x0000000100000000
        jal     cross_n64_spread
        nop
        lui     $12,%hi(cross_n64_spread_area)
        daddiu  $12,$12,%lo(cross_n64_spread_area)
        ld      $13,0($12)
        dli     $1,0x0000000100000000
        bne     $13,$1,.Lfail
        nop
        ld      $13,8($12)
        dli     $1,0x0000000200000001
        bne     $13,$1,.Lfail
        nop
        ld      $13,16($12)
        dli     $1,0x0000000300000002
        bne     $13,$1,.Lfail
        nop
        ld      $13,24($12)
        dli     $1,0x0000000400000003
        bne     $13,$1,.Lfail
        nop
        ld      $13,32($12)
        dli     $1,0x0000000500000004
        bne     $13,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 6: out channels are 64-bit pointers in a2/a3:
        # 0x123456789 = 4886718 * 1000 + 345.
        daddiu  $26,$0,'6'
        jal     set_sentinels
        nop
        dli     $4,0x0000000123456789
        daddiu  $5,$0,1000
        lui     $6,%hi(cross_n64_quotient)
        daddiu  $6,$6,%lo(cross_n64_quotient)
        lui     $7,%hi(cross_n64_remainder)
        daddiu  $7,$7,%lo(cross_n64_remainder)
        jal     cross_n64_divide
        nop
        lui     $12,%hi(cross_n64_quotient)
        daddiu  $12,$12,%lo(cross_n64_quotient)
        ld      $13,0($12)
        dli     $1,4886718
        bne     $13,$1,.Lfail
        nop
        lui     $12,%hi(cross_n64_remainder)
        daddiu  $12,$12,%lo(cross_n64_remainder)
        ld      $13,0($12)
        daddiu  $1,$0,345
        bne     $13,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 7: cross_n64_keep keeps five values live across two calls.
        daddiu  $26,$0,'7'
        jal     set_sentinels
        nop
        daddiu  $4,$0,1000
        jal     cross_n64_keep
        nop
        dli     $1,607861
        bne     $2,$1,.Lfail
        nop
        jal     check_sentinels
        nop

        # 8: an n64 entry calls the 24-argument Cross function with three
        # values live across it.
        daddiu  $26,$0,'8'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        dli     $4,0x0000000100000003
        jal     cross_n64_entry_weighted
        nop
        dli     $1,0x012191218998e443
        bne     $2,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
        nop

        # 9: floating Cross calls from n64: base 1.5 gives 318.5.
        daddiu  $26,$0,'9'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        dli     $1,0x3ff8000000000000
        dmtc1   $1,$f12
        jal     cross_n64_entry_floats
        nop
        dmfc1   $24,$f0
        dli     $1,0x4073e80000000000
        bne     $24,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
        nop

        # A: records, a hidden result pointer, and output channels.
        daddiu  $26,$0,'A'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        dli     $4,0x0000000200000005
        jal     cross_n64_entry_records
        nop
        dli     $1,0x000002976e1673b7
        bne     $2,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
        nop

        # B: indirect calls through Cross and n64 function-pointer types.
        daddiu  $26,$0,'B'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        dli     $4,0x0000000300000007
        jal     cross_n64_entry_indirect
        nop
        dli     $1,0x000180c90003b565
        bne     $2,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
        nop

        # C: a private helper receives a whole 64-bit pointer.
        daddiu  $26,$0,'C'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        lui     $4,%hi(cross_n64_cells)
        daddiu  $4,$4,%lo(cross_n64_cells)
        dli     $1,0x1111111111111111
        sd      $1,0($4)
        dli     $1,0x2222222222222222
        sd      $1,8($4)
        daddiu  $5,$0,0x1000
        jal     cross_n64_entry_private
        nop
        dli     $1,0x6666666666668667
        bne     $2,$1,.Lfail
        nop
        lui     $12,%hi(cross_n64_cells)
        daddiu  $12,$12,%lo(cross_n64_cells)
        ld      $13,16($12)
        dli     $1,0x3333333333334333
        bne     $13,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
        nop

        # D: an n64 entry calls cross_n64_clobber below, which clobbers
        # every register the Cross contract permits, gp and f24-f31
        # included.  n64 passes the f64 after one integer in f13.
        daddiu  $26,$0,'D'
        jal     set_sentinels
        nop
        jal     set_n64_sentinels
        nop
        dli     $4,0x0000000400000009
        dli     $1,0x3ff8000000000000
        dmtc1   $1,$f13
        jal     cross_n64_entry_bridge
        nop
        dli     $1,0x00000060000000da
        bne     $2,$1,.Lfail
        nop
        jal     check_sentinels
        nop
        jal     check_n64_sentinels
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

# Registers both ABIs preserve: s0-s7 ($16-$23) and the frame pointer $30
# receive 0x5e5e5e5e000000NN, NN being the register number.
set_sentinels:
        dli     $16,0x5e5e5e5e00000010
        dli     $17,0x5e5e5e5e00000011
        dli     $18,0x5e5e5e5e00000012
        dli     $19,0x5e5e5e5e00000013
        dli     $20,0x5e5e5e5e00000014
        dli     $21,0x5e5e5e5e00000015
        dli     $22,0x5e5e5e5e00000016
        dli     $23,0x5e5e5e5e00000017
        dli     $30,0x5e5e5e5e0000001e
        jr      $31
        nop

check_sentinels:
        dli     $1,0x5e5e5e5e00000010
        bne     $16,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000011
        bne     $17,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000012
        bne     $18,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000013
        bne     $19,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000014
        bne     $20,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000015
        bne     $21,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000016
        bne     $22,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e00000017
        bne     $23,$1,.Lfail
        nop
        dli     $1,0x5e5e5e5e0000001e
        bne     $30,$1,.Lfail
        nop
        jr      $31
        nop

# n64 also preserves gp ($28) and f24-f31, which Cross callees clobber.
set_n64_sentinels:
        dli     $28,0x5e5e5e5e0000001c
        dli     $1,0x5e5e5e5e00000038
        dmtc1   $1,$f24
        dli     $1,0x5e5e5e5e00000039
        dmtc1   $1,$f25
        dli     $1,0x5e5e5e5e0000003a
        dmtc1   $1,$f26
        dli     $1,0x5e5e5e5e0000003b
        dmtc1   $1,$f27
        dli     $1,0x5e5e5e5e0000003c
        dmtc1   $1,$f28
        dli     $1,0x5e5e5e5e0000003d
        dmtc1   $1,$f29
        dli     $1,0x5e5e5e5e0000003e
        dmtc1   $1,$f30
        dli     $1,0x5e5e5e5e0000003f
        dmtc1   $1,$f31
        jr      $31
        nop

check_n64_sentinels:
        dli     $1,0x5e5e5e5e0000001c
        bne     $28,$1,.Lfail
        nop
        dmfc1   $24,$f24
        dli     $1,0x5e5e5e5e00000038
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f25
        dli     $1,0x5e5e5e5e00000039
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f26
        dli     $1,0x5e5e5e5e0000003a
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f27
        dli     $1,0x5e5e5e5e0000003b
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f28
        dli     $1,0x5e5e5e5e0000003c
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f29
        dli     $1,0x5e5e5e5e0000003d
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f30
        dli     $1,0x5e5e5e5e0000003e
        bne     $24,$1,.Lfail
        nop
        dmfc1   $24,$f31
        dli     $1,0x5e5e5e5e0000003f
        bne     $24,$1,.Lfail
        nop
        jr      $31
        nop
        .end    _start

# A Cross-ABI function: value in a0, 2 * value + 1 in v0.  Every register
# the cross-n64 contract lets a callee clobber receives junk; s0-s7, sp, and
# fp stay intact.
        .text
        .globl  cross_n64_clobber
        .ent    cross_n64_clobber
cross_n64_clobber:
        daddu   $2,$4,$4
        daddiu  $2,$2,1
        dli     $1,0x0badc0de0badc0de
        move    $3,$1
        move    $4,$1
        move    $5,$1
        move    $6,$1
        move    $7,$1
        move    $8,$1
        move    $9,$1
        move    $10,$1
        move    $11,$1
        move    $12,$1
        move    $13,$1
        move    $14,$1
        move    $15,$1
        move    $24,$1
        move    $25,$1
        move    $28,$1
        mthi    $1
        mtlo    $1
        dmtc1   $1,$f0
        dmtc1   $1,$f1
        dmtc1   $1,$f2
        dmtc1   $1,$f3
        dmtc1   $1,$f4
        dmtc1   $1,$f5
        dmtc1   $1,$f6
        dmtc1   $1,$f7
        dmtc1   $1,$f8
        dmtc1   $1,$f9
        dmtc1   $1,$f10
        dmtc1   $1,$f11
        dmtc1   $1,$f12
        dmtc1   $1,$f13
        dmtc1   $1,$f14
        dmtc1   $1,$f15
        dmtc1   $1,$f16
        dmtc1   $1,$f17
        dmtc1   $1,$f18
        dmtc1   $1,$f19
        dmtc1   $1,$f20
        dmtc1   $1,$f21
        dmtc1   $1,$f22
        dmtc1   $1,$f23
        dmtc1   $1,$f24
        dmtc1   $1,$f25
        dmtc1   $1,$f26
        dmtc1   $1,$f27
        dmtc1   $1,$f28
        dmtc1   $1,$f29
        dmtc1   $1,$f30
        dmtc1   $1,$f31
        jr      $31
        nop
        .end    cross_n64_clobber

        .section .bss,"aw",@nobits
        .p2align 3
cross_n64_spread_area:
        .space  40
cross_n64_quotient:
        .space  8
cross_n64_remainder:
        .space  8
cross_n64_cells:
        .space  24
