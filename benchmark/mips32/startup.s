# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

        .set    noreorder
        .set    noat
        .set    mips3
        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $sp,0x8080
        addiu   $sp,$sp,-64

        # The Malta firmware-less entry leaves CP1 disabled.
        mfc0    $t0,$12
        lui     $t1,0x2000
        or      $t0,$t0,$t1
        mtc0    $t0,$12
        nop
        nop

        jal     benchmark_main
        nop
        move    $a0,$v0
        addiu   $v0,$zero,1
        addiu   $t9,$zero,1
        # Unified Hosting Interface exit trap.
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop
        .end    _start
