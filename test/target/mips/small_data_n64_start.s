# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# n64 startup for small data: $gp ($28) holds _gp before any code that
# reaches small data runs. Three rounds of small_data_check must return 1;
# the failure code is the round's digit. GPRs are numeric, as in
# n64_runtime.s.

        .set    noreorder
        .set    noat
        .set    mips64

        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64

        # Enable CU1 and FR for the n64 FPU.
        mfc0    $12,$12
        lui     $13,0x2400
        or      $12,$12,$13
        mtc0    $12,$12
        nop
        nop

        lui     $28,%hi(_gp)
        daddiu  $28,$28,%lo(_gp)

        daddiu  $26,$0,'0'
        move    $4,$0
        jal     small_data_check
        nop
        daddiu  $12,$0,1
        bne     $2,$12,.Lfail
        nop
        daddiu  $26,$0,'1'
        daddiu  $4,$0,1
        jal     small_data_check
        nop
        daddiu  $12,$0,1
        bne     $2,$12,.Lfail
        nop
        daddiu  $26,$0,'2'
        daddiu  $4,$0,2
        jal     small_data_check
        nop
        daddiu  $12,$0,1
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
        lui     $12,0xbf00
        ori     $12,$12,0x0900
        sb      $4,0($12)
        daddiu  $4,$0,'\n'
        sb      $4,0($12)
        move    $4,$14
        daddiu  $2,$0,1
        daddiu  $25,$0,1
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop
        .end    _start
