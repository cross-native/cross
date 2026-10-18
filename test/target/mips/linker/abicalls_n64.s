# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# n64 abicalls relocations: $gp from %neg(%gp_rel(...)) and from
# __gnu_local_gp, GOT page entries of local symbols completed by
# %got_ofst, displacement entries of an object and a function of another
# object, also through the large-GOT operators, JALR hints that the linker
# may turn into branches, .gpdword and .gpword, and TLS: one initial-exec
# GOT entry, local-exec operators, and TP- and DTP-relative doublewords.

        .abicalls
        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $28,%hi(%neg(%gp_rel(_start)))
        daddu   $28,$28,$25
        daddiu  $28,$28,%lo(%neg(%gp_rel(_start)))
        lui     $2,%hi(__gnu_local_gp)
        daddiu  $2,$2,%lo(__gnu_local_gp)
        ld      $3,%got_page(local_data)($28)
        daddiu  $3,$3,%got_ofst(local_data)
        ld      $4,%got_page(local_data+0x10010)($28)
        lw      $4,%got_ofst(local_data+0x10010)($4)
        ld      $5,%got_disp(shared_data)($28)
        ld      $5,%got_disp(local_data)($28)
        ld      $25,%call16(far_function)($28)
        .reloc  1f,R_MIPS_JALR,far_function
1:      jalr    $25
        nop
        ld      $25,%got_disp(local_function)($28)
        .reloc  2f,R_MIPS_JALR,local_function
2:      jalr    $25
        nop
        lui     $6,%got_hi(shared_data)
        daddu   $6,$6,$28
        ld      $6,%got_lo(shared_data)($6)
        lui     $7,%call_hi(far_function)
        daddu   $7,$7,$28
        ld      $7,%call_lo(far_function)($7)
        ld      $8,%gottprel(tls_shared)($28)
        lui     $9,%tprel_hi(tls_local)
        daddiu  $9,$9,%tprel_lo(tls_local)
        lui     $10,%tprel_hi(tls_shared+4)
        daddiu  $10,$10,%tprel_lo(tls_shared+4)
        jr      $31
        nop
        .end    _start

local_function:
        jr      $31
        nop

        .rdata
        .align  3
table:  .gpdword local_function
        .gpdword far_function
        .gpword local_function

        .data
        .align  3
local_data:
        .word   1
        .space  0x10010
        .word   2
        .tpreldword tls_local
        .dtpreldword tls_shared+4

        .section .tdata,"awT",@progbits
        .align  2
tls_local:
        .word   7
        .section .tbss,"awT",@nobits
        .align  4
tls_zero:
        .space  8
