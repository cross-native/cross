# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# R_MIPS_LITERAL, which GNU as emits for small-data floating literals (-G 8).

        .set    noreorder
        .text
        .globl  _start
_start:
        li.d    $f2, 1.1
        li.s    $f0, 1.1
        jr      $31
        nop
