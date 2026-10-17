# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# MIPS III forms GNU as accepts for the VR4300 with 32-bit GPRs: mul as
# multu and mflo, a constant multiplier through $at, doubleword shifts by a
# register, doubleword operations with a constant, and traps; and the
# coprocessor 0 instructions an N64 runtime uses.
        .set noreorder
        .text
        mul     $2, $4, $5              # 00850019 00001012
        mul     $2, $4, -3              # 2401fffd 00810018 00001012
        dsll    $2, $3, $4              # 00831014
        dsrl    $2, $3, $4              # 00831016
        dsra    $2, $3, $4              # 00831017
        dsll    $2, $3, 40              # 0003123c
        daddu   $2, $3, 100             # 64620064
        dsubu   $2, $3, 100             # 6462ff9c
        daddu   $2, $3, 0x123456        # 3c010012 34213456 0061102d
        teq     $0, $0                  # 00000034
        tltu    $4, 0x7fff              # 048b7fff
        mfc0    $2, $12                 # 40026000
        mtc0    $2, $12                 # 40826000
        dmfc0   $2, $14                 # 40227000
        dmtc0   $2, $14                 # 40a27000
        cache   0x18, 0($4)             # bc980000
        cache   0x14, -16($8)           # bd14fff0
        tlbp                            # 42000008
        tlbr                            # 42000001
        tlbwi                           # 42000002
        tlbwr                           # 42000006
        eret                            # 42000018
        sync                            # 0000000f
