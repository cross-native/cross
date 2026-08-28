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

        # QEMU's firmware enters with CP1 disabled. Real startup code owns
        # this policy too; the compiler never enables hardware implicitly.
        mfc0    $t0,$12
        lui     $t1,0x2000
        or      $t0,$t0,$t1
        mtc0    $t0,$12
        nop
        nop

        # i64 values use high-word-first o32 slots on big-endian MIPS.
        addiu   $k0,$zero,'1'
        move    $a0,$zero
        addiu   $a1,$zero,1
        move    $a2,$zero
        addiu   $a3,$zero,2
        addiu   $t0,$zero,1
        sw      $t0,16($sp)
        jal     mips_entry
        nop
        bne     $v0,$zero,.Lfail
        nop
        addiu   $t0,$zero,132
        bne     $v1,$t0,.Lfail
        nop

        # `lw` sign-extends on MIPS III; an unsigned widening conversion must
        # explicitly clear that physical-register high half before return.
        addiu   $k0,$zero,'2'
        lui     $a0,0xf1cd
        ori     $a0,$a0,0x194f
        jal     mips_widen_u32
        nop
        bne     $v0,$zero,.Lfail
        nop
        lui     $t0,0xf1cd
        ori     $t0,$t0,0x194f
        bne     $v1,$t0,.Lfail
        nop

        addiu   $k0,$zero,'3'
        lui     $a0,%hi(runtime_words)
        addiu   $a0,$a0,%lo(runtime_words)
        addiu   $a1,$zero,1
        addiu   $a2,$zero,41
        jal     mips_memory
        nop
        addiu   $t0,$zero,42
        bne     $v0,$t0,.Lfail
        nop
        addiu   $k0,$zero,'4'
        lui     $a0,%hi(runtime_words)
        addiu   $a0,$a0,%lo(runtime_words)
        lw      $t1,4($a0)
        addiu   $t0,$zero,41
        bne     $t1,$t0,.Lfail
        nop

        addiu   $k0,$zero,'5'
        addiu   $a0,$zero,5
        jal     mips_atomic_add
        nop
        bne     $v0,$zero,.Lfail
        nop
        addiu   $k0,$zero,'6'
        lui     $t0,%hi(mips_atomic)
        addiu   $t0,$t0,%lo(mips_atomic)
        lw      $t1,0($t0)
        addiu   $t0,$zero,5
        bne     $t1,$t0,.Lfail
        nop

        addiu   $k0,$zero,'7'
        jal     mips_patch_value
        nop
        lui     $t0,0x479e
        ori     $t0,$t0,0x03d2
        bne     $v0,$t0,.Lfail
        nop

        # Execute the MIPS-I word-pair legalization on a big-endian target.
        # The same source is also run little-endian on Allegrex/PPSSPP.
        addiu   $k0,$zero,'8'
        jal     mips_pair_entry
        nop
        lui     $t0,0x7
        ori     $t0,$t0,0xffff
        bne     $v0,$t0,.Lfail
        nop

        # A surviving private call uses Cross64 even though this public test
        # boundary is o32. The fifth full-width argument travels through t0.
        addiu   $k0,$zero,'9'
        addiu   $a0,$zero,7
        jal     mips_private_entry
        nop
        bne     $v0,$zero,.Lfail
        nop
        addiu   $t0,$zero,21
        bne     $v1,$t0,.Lfail
        nop

        addiu   $a0,$zero,'P'
        b       .Lreport
        nop
.Lfail:
        move    $a0,$k0
.Lreport:
        xori    $t2,$a0,'P'
        sltu    $t2,$zero,$t2
        # Malta's first 16550 UART is visible through uncached KSEG1.
        lui     $t0,0xbf00
        ori     $t0,$t0,0x0900
        sb      $a0,0($t0)
        addiu   $a0,$zero,'\n'
        sb      $a0,0($t0)
        move    $a0,$t2
        addiu   $v0,$zero,1
        addiu   $t9,$zero,1
        # SDBBP 1 is the Unified Hosting Interface trap. Spell its fixed
        # encoding because the R4000 assembler mode predates the mnemonic.
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop
        .end    _start

        .section .bss,"aw",@nobits
        .p2align 3
runtime_words:
        .space  16
