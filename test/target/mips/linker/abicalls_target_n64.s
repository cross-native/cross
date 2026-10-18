# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The other object of abicalls_n64.s: a global function that sets up $gp
# and reads a local object through a GOT page entry, a global object, and
# a global thread-local object.

        .abicalls
        .set    noreorder
        .text
        .globl  far_function
        .ent    far_function
far_function:
        lui     $28,%hi(%neg(%gp_rel(far_function)))
        daddu   $28,$28,$25
        daddiu  $28,$28,%lo(%neg(%gp_rel(far_function)))
        ld      $2,%got_page(partner_local)($28)
        lw      $2,%got_ofst(partner_local)($2)
        jr      $31
        nop
        .end    far_function

        .data
        .globl  shared_data
shared_data:
        .word   3
partner_local:
        .word   4

        .section .tbss,"awT",@nobits
        .globl  tls_shared
        .align  2
tls_shared:
        .space  8
