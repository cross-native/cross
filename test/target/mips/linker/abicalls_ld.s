# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Local-dynamic TLS in a static link: the module's GOT entry pair and
# DTP-relative operators of a local thread-local object.

        .abicalls
        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        addiu   $4,$28,%tlsldm(tls_local)
        lui     $9,%dtprel_hi(tls_local)
        addiu   $9,$9,%dtprel_lo(tls_local)
        jr      $31
        nop

        .section .tdata,"awT",@progbits
tls_local:
        .word   7
