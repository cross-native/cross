# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# SVR4 PIC (abicalls) and TLS in an n64 object: the GOT page, offset, and
# displacement operators, %neg(%gp_rel(...)), .cpsetup, .cplocal, and
# .cpreturn, the PIC `jal` and `j` macros, .gpdword, and the TLS
# operators and data.

        .abicalls
        .text
        .globl  gfun
        .ent    f
f:
        .frame  $sp,32,$31
        .set    noreorder
        .set    nomacro
        daddiu  $sp,$sp,-32
        sd      $28,16($sp)
        lui     $28,%hi(%neg(%gp_rel(f)))
        daddu   $28,$28,$25
        daddiu  $28,$28,%lo(%neg(%gp_rel(f)))
        ld      $2,%got_page(local_var)($28)
        lw      $2,%got_ofst(local_var)($2)
        ld      $3,%got_page(local_var+0x12344)($28)
        daddiu  $3,$3,%got_ofst(local_var+0x12344)
        ld      $4,%got_disp(ext_var)($28)
        ld      $4,%got_disp(local_var)($28)
        ld      $25,%call16(ext_fn)($28)
        .reloc  1f,R_MIPS_JALR,ext_fn
1:      jalr    $25
        nop
        ld      $25,%got_page(f)($28)
        daddiu  $25,$25,%got_ofst(f)
        ld      $5,%got(ext_var)($28)
        lui     $5,%got_hi(ext_var)
        daddu   $5,$5,$28
        ld      $5,%got_lo(ext_var)($5)
        ld      $7,%gottprel(tls_var)($28)
        lui     $8,%tprel_hi(tls_var)
        daddiu  $8,$8,%tprel_lo(tls_var)
        daddiu  $4,$28,%tlsgd(tls_var)
        daddiu  $4,$28,%tlsldm(tls_var)
        lui     $9,%dtprel_hi(tls_var)
        daddiu  $9,$9,%dtprel_lo(tls_var)
        lw      $10,%gp_rel(local_var)($28)
        lui     $11,%hi(local_var)
        ld      $3,%got_page(string+1)($28)
        daddiu  $3,$3,%got_ofst(string+1)
        .set    push
        .set    mips32r2
        rdhwr   $3,$29
        .set    pop
        ld      $28,16($sp)
        jr      $31
        daddiu  $sp,$sp,32
        .set    macro
        .set    reorder
        .end    f
gfun:   jr      $31

local:  jr      $31
        .globl  g
        .ent    g
g:
        .set    noreorder
        .cpsetup $25,8,g
        ld      $2,%got_disp(ext_var)($28)
        .cpreturn
        .cplocal $3
        .cpsetup $25,$12,g
        .cpreturn
        .set    reorder
        jal     ext_fn
        jal     local
        jal     later
        j       local
        .end    g
later:  jr      $31

        .rdata
        .align  3
table:  .gpdword f
        .gpdword local_var
        .gpword f
        .data
local_var:
        .word   1, 2
        .dtpreldword tls_var
        .dtprelword tls_var
        .section .rodata.str,"aMS",@progbits,1
string: .asciz  "abc"
        .section .tbss,"awT",@nobits
        .type   tls_var,@object
tls_var:
        .space  4
