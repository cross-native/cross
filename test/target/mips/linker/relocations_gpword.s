# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# R_MIPS_GPREL32, which GNU as emits for .gpword (-KPIC).

        .text
        .globl  _start
_start:
        jr      $31
        nop

        .rdata
table:
        .gpword _start
        .gpword local_label
local_label:
        .word   0
