# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# n64 startup for qemu-system-mips64 -M malta: entry(5), then spin. GPRs are
# numbered because n64 assemblers name registers 8-15 differently.
        .set    noreorder
        .set    mips64
        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64
        jal     entry
        daddiu  $4,$0,5
1:      b       1b
        nop
        .end    _start
