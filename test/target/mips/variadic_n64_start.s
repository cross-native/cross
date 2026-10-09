# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Calls variadic_entry under qemu-system-mips64 -M malta and prints P when it
# returns 0 with $sp intact, or F and the failing result in hex. GPRs are
# numeric: $2 result, $16-$19 saved, $29 sp.

        .set    noreorder
        .set    noat
        .set    mips64

        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64
        # Enable CU1 and FR: the 64-bit address model uses the 64-bit FPU.
        mfc0    $12,$12
        lui     $13,0x2400
        or      $12,$12,$13
        mtc0    $12,$12
        nop
        nop
        move    $16,$29
        jal     variadic_entry
        nop
        bne     $29,$16,.Lfail
        nop
        bne     $2,$0,.Lfail
        nop
        lui     $18,0xbf00
        ori     $18,$18,0x0900
        daddiu  $12,$0,'P'
        b       .Lfinish
        sb      $12,0($18)
.Lfail:
        move    $17,$2
        lui     $18,0xbf00
        ori     $18,$18,0x0900
        daddiu  $12,$0,'F'
        sb      $12,0($18)
        daddiu  $19,$0,8
.Ldigit:
        srl     $12,$17,28
        sll     $17,$17,4
        sltiu   $13,$12,10
        bne     $13,$0,.Ldecimal
        daddiu  $12,$12,'0'
        daddiu  $12,$12,'a'-'0'-10
.Ldecimal:
        sb      $12,0($18)
        daddiu  $19,$19,-1
        bne     $19,$0,.Ldigit
        nop
.Lfinish:
        daddiu  $12,$0,'\n'
        sb      $12,0($18)
        move    $4,$0
        daddiu  $2,$0,1
        daddiu  $25,$0,1
        # SDBBP 1, the Unified Hosting Interface trap.
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop
        .end    _start
