# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Global-dynamic TLS in a static link: the GOT entry pair of a global
# thread-local object of abicalls_target_o32.s, and DTP-relative operators.

        .abicalls
        .set    noreorder
        .section .text.boot,"ax",@progbits
        .globl  _start
_start:
        addiu   $4,$28,%tlsgd(tls_shared)
        lui     $9,%dtprel_hi(tls_shared+4)
        addiu   $9,$9,%dtprel_lo(tls_shared+4)
        jr      $31
        nop
