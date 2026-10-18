# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# o32 abicalls relocations: $gp from _gp_disp and from __gnu_local_gp, GOT
# page entries of local symbols completed by %lo, global entries of an
# object and a function of another object, also through the large-GOT
# operators, JALR hints that the linker may turn into branches, .gpword,
# and TLS: one initial-exec GOT entry, local-exec operators, and TP- and
# DTP-relative words.

        .abicalls
        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $28,%hi(_gp_disp)
        addiu   $28,$28,%lo(_gp_disp)
        addu    $28,$28,$25
        lui     $2,%hi(__gnu_local_gp)
        addiu   $2,$2,%lo(__gnu_local_gp)
        lw      $3,%got(local_data)($28)
        addiu   $3,$3,%lo(local_data)
        lw      $4,%got(local_data+0x10010)($28)
        lw      $4,%lo(local_data+0x10010)($4)
        lw      $5,%got(shared_data)($28)
        lw      $25,%call16(far_function)($28)
        .reloc  1f,R_MIPS_JALR,far_function
1:      jalr    $25
        nop
        lw      $25,%got(local_function)($28)
        addiu   $25,$25,%lo(local_function)
        .reloc  2f,R_MIPS_JALR,local_function
2:      jalr    $25
        nop
        lui     $6,%got_hi(shared_data)
        addu    $6,$6,$28
        lw      $6,%got_lo(shared_data)($6)
        lui     $7,%call_hi(far_function)
        addu    $7,$7,$28
        lw      $7,%call_lo(far_function)($7)
        lw      $8,%gottprel(tls_shared)($28)
        lui     $9,%tprel_hi(tls_local)
        addiu   $9,$9,%tprel_lo(tls_local)
        lui     $10,%tprel_hi(tls_shared+4)
        addiu   $10,$10,%tprel_lo(tls_shared+4)
        jr      $31
        nop
        .end    _start

local_function:
        jr      $31
        nop

        .rdata
        .align  2
table:  .gpword local_function
        .gpword far_function

        .data
local_data:
        .word   1
        .space  0x10010
        .word   2
        .tprelword tls_local
        .dtprelword tls_shared+4

        .section .tdata,"awT",@progbits
        .align  2
tls_local:
        .word   7
        .section .tbss,"awT",@nobits
        .align  3
tls_zero:
        .space  8
