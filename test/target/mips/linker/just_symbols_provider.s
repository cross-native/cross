# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

        .text
        .set    noreorder
        .globl  imported
        .ent    imported
imported:
        jr      $31
        nop
        .end    imported
