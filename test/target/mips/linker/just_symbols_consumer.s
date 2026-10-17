# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

        .text
        .set    noreorder
        .globl  _start
        .ent    _start
_start:
        jal     imported
        nop
        .end    _start
