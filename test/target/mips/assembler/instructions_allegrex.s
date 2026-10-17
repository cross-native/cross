# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The Allegrex instructions: its own encodings of clz, clo, and the
# multiply-accumulate family, minimum and maximum, bit fields, byte
# reversals, rotates, the interrupt control register, and the coprocessor 0
# cache and exception return it shares with MIPS III.
        .set noreorder
        .text
        clz     $2, $3                  # 00601016
        clo     $2, $3                  # 00601017
        madd    $4, $5                  # 0085001c
        maddu   $4, $5                  # 0085001d
        msub    $4, $5                  # 0085002e
        msubu   $4, $5                  # 0085002f
        max     $2, $3, $4              # 0064102c
        min     $2, $3, $4              # 0064102d
        ext     $2, $3, 4, 8            # 7c623900
        ins     $2, $3, 4, 8            # 7c625904
        seb     $2, $3                  # 7c031420
        seh     $2, $3                  # 7c031620
        wsbh    $2, $3                  # 7c0310a0
        wsbw    $2, $3                  # 7c0310e0
        bitrev  $2, $3                  # 7c031520
        rotr    $2, $3, 4               # 00231102
        rotrv   $2, $3, $4              # 00831046
        ror     $2, $3, 4               # 00231102
        rorv    $2, $3, $4              # 00831046
        rotr    $2, $3, $4              # 00831046
        mfic    $2, $0                  # 70020024
        mtic    $2, $0                  # 70020026
        cache   0x18, 0($4)             # bc980000
        eret                            # 42000018
