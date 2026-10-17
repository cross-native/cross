# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# FPRs named `$N` in loads, stores, and moves, where GNU as reads a
# coprocessor register: an odd number may hold a double half, and the
# register counts in no FPR mask of .reginfo, which only names the `$fN`
# operands.
        .set noreorder
        .text
        lwc1    $4, 0x1c($5)            # c4a4001c
        swc1    $20, 0x2c($4)           # e494002c
        ldc1    $3, 8($6)               # d4c30008
        sdc1    $30, 0x1a8($26)         # f75e01a8
        mtc1    $8, $6                  # 44883000
        mfc1    $6, $7                  # 44063800
        dmtc1   $2, $9                  # 44a24800
        dmfc1   $2, $11                 # 44225800
        lwc1    $f4, 0($5)              # c4a40000
        add.s   $f0, $f2, $f4           # 46041000
        ldc1    $f8, 16($5)             # d4a80010
