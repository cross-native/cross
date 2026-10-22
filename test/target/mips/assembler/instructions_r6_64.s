# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# MIPS64 Release 6 instructions: the doubleword multiply, divide, scaled
# add, upper-immediate adds, byte alignment, and bit reversal, the
# re-encoded DCLZ, DCLO, LLD, and SCD, and the doubleword macros whose
# immediate forms go through $at.
        .set noreorder
        .text
        dmul    $2, $4, $5              # 0085109c
        dmuh    $2, $4, $5              # 008510dc
        dmulu   $2, $4, $5              # 0085109d
        dmuhu   $2, $4, $5              # 008510dd
        ddiv    $2, $4, $5              # 0085109e
        dmod    $2, $4, $5              # 008510de
        ddivu   $2, $4, $5              # 0085109f
        dmodu   $2, $4, $5              # 008510df
        mul     $2, $4, $5              # 00851098
        div     $2, $4, $5              # 0085109a
        dlsa    $2, $3, $4, 1           # 00641015
        dlsa    $2, $3, $4, 3           # 00641095
        daui    $2, $3, 0x1234          # 74621234
        dahi    $4, $4, 0x1234          # 04861234
        dati    $4, $4, 0xffff          # 049effff
        dalign  $2, $3, $4, 0           # 7c641224
        dalign  $2, $3, $4, 7           # 7c6413e4
        dbitswap $2, $3                 # 7c031024
        dclz    $2, $3                  # 00601052
        dclo    $2, $3                  # 00601053
        clz     $2, $3                  # 00601050
        lld     $2, 8($4)               # 7c820437
        lld     $2, -256($4)            # 7c828037
        scd     $2, 255($4)             # 7c827fa7
        ll      $2, 8($4)               # 7c820436
        sc      $2, 8($4)               # 7c820426
        seleqz  $2, $4, $5              # 00851035
        selnez  $2, $4, $5              # 00851037
        dadd    $2, $3, 5               # 24010005 0061102c
        dsub    $2, $3, 5               # 24010005 0061102e
        dsll    $2, $3, 4               # 00031138
        drotr   $2, $3, 4               # 0023113a
        dext    $2, $3, 4, 8            # 7c623903
        dins    $2, $3, 4, 8            # 7c625907
        dmfc1   $2, $f3                 # 44221800
        dmtc1   $2, $f3                 # 44a21800
        jr      $31                     # 03e00009
        jalr    $25                     # 0320f809
        cmp.lt.d $f1, $f3, $f5          # 46a51844
