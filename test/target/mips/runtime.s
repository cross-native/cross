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

        # Mixed-width XOR is returned by composing the two o32 result words
        # directly. Exercise a low word with bit 31 set before the XOR so the
        # optimization cannot rely on physical zero-canonical inputs.
        addiu   $k0,$zero,'B'
        lui     $a0,0x0123
        ori     $a0,$a0,0x4567
        lui     $a1,0x89ab
        ori     $a1,$a1,0xcdef
        lui     $a2,0x8000
        jal     mips_mixed_xor
        nop
        lui     $t0,0x0123
        ori     $t0,$t0,0x4567
        bne     $v0,$t0,.Lfail
        nop
        lui     $t0,0x09ab
        ori     $t0,$t0,0xcdef
        bne     $v1,$t0,.Lfail
        nop

        addiu   $k0,$zero,'C'
        lui     $a0,0x8000
        lui     $a1,0x8000
        lui     $a2,0x8000
        jal     mips_mixed_xor_commuted
        nop
        lui     $t0,0x8000
        bne     $v0,$t0,.Lfail
        nop
        bne     $v1,$zero,.Lfail
        nop

        # SImode is sign-canonical internally, but an unsigned FPU
        # conversion must consume the zero-extended mathematical value.
        addiu   $k0,$zero,'F'
        lui     $a0,0x8000
        jal     mips_u32_to_f64
        nop
        lui     $t0,%hi(runtime_words)
        addiu   $t0,$t0,%lo(runtime_words)
        sdc1    $f0,0($t0)
        lw      $t2,0($t0)
        lui     $t1,0x41e0
        bne     $t2,$t1,.Lfail
        nop
        lw      $t2,4($t0)
        bne     $t2,$zero,.Lfail
        nop

        # The high-range halving expansion must round an odd integer upward
        # before doubling.  0xffffffff is exactly representable as f64.
        addiu   $k0,$zero,'G'
        addiu   $a0,$zero,-1
        jal     mips_u32_to_f64
        nop
        lui     $t0,%hi(runtime_words)
        addiu   $t0,$t0,%lo(runtime_words)
        sdc1    $f0,0($t0)
        lw      $t2,0($t0)
        lui     $t1,0x41ef
        ori     $t1,$t1,0xffff
        bne     $t2,$t1,.Lfail
        nop
        lw      $t2,4($t0)
        lui     $t1,0xffe0
        bne     $t2,$t1,.Lfail
        nop

        # FP32 mode has no legal long-FPR conversion in ordinary execution.
        # Exercise both sides of the runtime-free split-at-2^31 u32 lowering.
        addiu   $k0,$zero,'U'
        lui     $t0,0x42f7             # 123.75f32
        ori     $t0,$t0,0x8000
        mtc1    $t0,$f12
        nop
        jal     mips_f32_to_u32
        nop
        addiu   $t0,$zero,123
        bne     $v0,$t0,.Lfail
        nop

        addiu   $k0,$zero,'V'
        lui     $t0,0x4f7f             # 4294967040.0f32
        ori     $t0,$t0,0xffff
        mtc1    $t0,$f12
        nop
        jal     mips_f32_to_u32
        nop
        addiu   $t0,$zero,-256
        bne     $v0,$t0,.Lfail
        nop

        addiu   $k0,$zero,'W'
        lui     $t0,0x4f00             # +2147483648.0f32
        mtc1    $t0,$f12
        nop
        cvt.d.s $f12,$f12
        jal     mips_f64_to_u32
        nop
        lui     $t0,0x8000
        bne     $v0,$t0,.Lfail
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

        addiu   $k0,$zero,'D'
        jal     mips_direct_load
        nop
        addiu   $t0,$zero,41
        bne     $v0,$t0,.Lfail
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
        lui     $t0,0xf
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

        # A loop index read after its increment must keep its own register.
        # mips_phi_after_use(1, &phi_words) stores the old index 0 and
        # returns 1.
        addiu   $k0,$zero,'X'
        addiu   $a0,$zero,1
        lui     $a1,%hi(phi_words)
        addiu   $a1,$a1,%lo(phi_words)
        jal     mips_phi_after_use
        nop
        addiu   $t0,$zero,1
        bne     $v0,$t0,.Lfail
        nop
        lui     $t0,%hi(phi_words)
        addiu   $t0,$t0,%lo(phi_words)
        lw      $t1,0($t0)
        bne     $t1,$zero,.Lfail
        nop

        # mips_phi_after_arith(3, 1) = ((1*3+0)*3+1)*3+2 = 32; a coalesced
        # remainder iteration would add the incremented index instead.
        addiu   $k0,$zero,'Y'
        addiu   $a0,$zero,3
        addiu   $a1,$zero,1
        jal     mips_phi_after_arith
        nop
        addiu   $t0,$zero,32
        bne     $v0,$t0,.Lfail
        nop

        # MIPS I f64 through lwc1/swc1 pairs: mips_pair_f64(3.0, 4.0, 2)
        # = (3*4 + 2.5)*2 - 3 = 26.0 = 0x403a000000000000, returned in
        # the f0/f1 pair with the high word in the odd register.
        addiu   $k0,$zero,'Z'
        mtc1    $zero,$f12
        lui     $t0,0x4008
        mtc1    $t0,$f13
        mtc1    $zero,$f14
        lui     $t0,0x4010
        mtc1    $t0,$f15
        addiu   $t0,$zero,2
        sw      $t0,16($sp)
        jal     mips_pair_f64
        nop
        mfc1    $t1,$f1
        lui     $t0,0x403a
        bne     $t1,$t0,.Lfail
        nop
        mfc1    $t2,$f0
        nop
        bne     $t2,$zero,.Lfail
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
phi_words:
        .space  8
