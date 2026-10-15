# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# o32 startup for qemu-system-mips -M malta: entry(5), then spin.
        .set    noreorder
        .set    mips1
        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $sp,0x8080
        addiu   $sp,$sp,-64
        jal     entry
        addiu   $a0,$zero,5
1:      b       1b
        nop
        .end    _start
