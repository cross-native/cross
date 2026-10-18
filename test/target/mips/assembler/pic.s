# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# SVR4 PIC (abicalls) and TLS in an o32 object: the GOT and call operators
# and their large-GOT forms, .cpload and .cprestore, the PIC `jal` and `j`
# macros, .reloc, .gpword, and the TLS operators, data, and symbols.

        .abicalls
        .text
        .globl  gfun
        .ent    f
f:
        .frame  $sp,32,$31
        .set    noreorder
        .cpload $25
        .set    nomacro
        addiu   $sp,$sp,-32
        .cprestore 16
        lw      $2,%got(local_var)($28)
        lw      $2,%lo(local_var)($2)
        lw      $3,%got(local_var+0x12344)($28)
        addiu   $3,$3,%lo(local_var+0x12344)
        lw      $4,%got(ext_var)($28)
        lw      $25,%call16(ext_fn)($28)
        .reloc  1f,R_MIPS_JALR,ext_fn
1:      jalr    $25
        nop
        lw      $28,16($sp)
        lw      $25,%got(f)($28)
        addiu   $25,$25,%lo(f)
        lui     $5,%got_hi(ext_var)
        addu    $5,$5,$28
        lw      $5,%got_lo(ext_var)($5)
        lui     $6,%call_hi(ext_fn)
        lw      $6,%call_lo(ext_fn)($6)
        lw      $7,%gottprel(tls_var)($28)
        lui     $8,%tprel_hi(tls_var)
        addiu   $8,$8,%tprel_lo(tls_var)
        addiu   $4,$28,%tlsgd(tls_var)
        addiu   $4,$28,%tlsldm(tls_var)
        lui     $9,%dtprel_hi(tls_var)
        addiu   $9,$9,%dtprel_lo(tls_var)
        lw      $10,%gp_rel(local_var)($28)
        lw      $2,%got(ext_var+8)($28)
        lw      $3,%got(string+1)($28)
        addiu   $3,$3,%lo(string+1)
        lw      $7,%gottprel(tls_ext)($28)
        lui     $8,%tprel_hi(tls_ext+4)
        addiu   $8,$8,%tprel_lo(tls_ext+4)
        .set    push
        .set    mips32r2
        rdhwr   $3,$29
        .set    pop
        jr      $31
        addiu   $sp,$sp,32
        .set    macro
        .set    reorder
        .end    f
gfun:   jr      $31

local:  jr      $31
        .ent    g
g:
        .frame  $sp,32,$31
        .set    noreorder
        .cpload $25
        .set    reorder
        addiu   $sp,$sp,-32
        .cprestore 16
        jal     ext_fn
        jal     local
        jal     ext_fn+8
        j       local
        .set    noreorder
        jal     ext_fn
        nop
        j       g
        nop
        .reloc  .,R_MIPS_JALR,f
        jalr    $25
        nop
        .set    reorder
        .end    g

        .rdata
        .align  2
table:  .gpword f
        .gpword local_var
        .data
local_var:
        .word   1, 2
        .dtprelword tls_var
        .dtprelword tls_init+4
        .tprelword tls_global
        .section .rodata.str,"aMS",@progbits,1
string: .asciz  "abc"
        .section .tbss,"awT",@nobits
        .type   tls_var,@object
tls_var:
        .space  4
        .section .tdata,"awT",@progbits
tls_init:
        .word   7
        .globl  tls_global
tls_global:
        .word   2
