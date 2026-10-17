# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# MIPS32 Release 2 instructions and the operand forms GNU as accepts:
# multiply-accumulate, bit fields, byte and halfword extension, prefetch,
# traps with registers, codes, and constants, the rounding conversions, every
# FPU comparison, shifts and rotates by a register, and three-register
# operations whose last operand is a constant, through $at when it does not
# fit the immediate instruction.
        .set noreorder
        .text
        madd    $4, $5                  # 70850000
        maddu   $4, $5                  # 70850001
        msub    $4, $5                  # 70850004
        msubu   $4, $5                  # 70850005
        mul     $2, $4, $5              # 70851002
        ext     $2, $3, 4, 8            # 7c623900
        ins     $2, $3, 4, 8            # 7c625904
        ext     $2, $3, 0, 32           # 7c62f800
        ins     $2, $3, 31, 1           # 7c62ffc4
        seb     $2, $3                  # 7c031420
        seh     $2, $3                  # 7c031620
        wsbh    $2, $3                  # 7c0310a0
        pref    0, 8($4)                # cc800008
        pref    30, -4($5)              # ccbefffc
        teq     $4, $5                  # 00850034
        teq     $4, $5, 7               # 008501f4
        tne     $4, $5                  # 00850036
        tge     $4, $5                  # 00850030
        tgeu    $4, $5                  # 00850031
        tlt     $4, $5                  # 00850032
        tltu    $4, $5                  # 00850033
        teq     $4, 100                 # 048c0064
        tne     $4, -3                  # 048efffd
        tge     $4, 5                   # 04880005
        tgeu    $4, 6                   # 04890006
        tlt     $4, 7                   # 048a0007
        tltu    $4, 8                   # 048b0008
        teqi    $4, 9                   # 048c0009
        tnei    $4, 10                  # 048e000a
        tgei    $4, 11                  # 0488000b
        tgeiu   $4, 12                  # 0489000c
        tlti    $4, 13                  # 048a000d
        tltiu   $4, 14                  # 048b000e
        teq     $4, 0x12345             # 3c010001 34212345 00810034
        round.w.s $f0, $f2              # 4600100c
        round.w.d $f0, $f2              # 4620100c
        ceil.w.s $f0, $f2               # 4600100e
        ceil.w.d $f0, $f2               # 4620100e
        floor.w.s $f0, $f2              # 4600100f
        floor.w.d $f0, $f2              # 4620100f
        round.l.s $f0, $f2              # 46001008
        round.l.d $f0, $f2              # 46201008
        ceil.l.s $f0, $f2               # 4600100a
        ceil.l.d $f0, $f2               # 4620100a
        floor.l.s $f0, $f2              # 4600100b
        floor.l.d $f0, $f2              # 4620100b
        cvt.w.s $f0, $f2                # 46001024
        cvt.w.d $f0, $f2                # 46201024
        cvt.l.s $f0, $f2                # 46001025
        cvt.l.d $f0, $f2                # 46201025
        c.f.s   $f2, $f4                # 46041030
        c.un.s  $f2, $f4                # 46041031
        c.eq.s  $f2, $f4                # 46041032
        c.ueq.s $f2, $f4                # 46041033
        c.olt.s $f2, $f4                # 46041034
        c.ult.s $f2, $f4                # 46041035
        c.ole.s $f2, $f4                # 46041036
        c.ule.s $f2, $f4                # 46041037
        c.sf.s  $f2, $f4                # 46041038
        c.ngle.s $f2, $f4               # 46041039
        c.seq.s $f2, $f4                # 4604103a
        c.ngl.s $f2, $f4                # 4604103b
        c.lt.s  $f2, $f4                # 4604103c
        c.nge.s $f2, $f4                # 4604103d
        c.le.s  $f2, $f4                # 4604103e
        c.ngt.s $f2, $f4                # 4604103f
        c.ueq.d $f2, $f4                # 46241033
        c.ngt.d $f2, $f4                # 4624103f
        c.ult.s $fcc3, $f2, $f4         # 46041335
        sll     $2, $3, $4              # 00831004
        srl     $2, $3, $4              # 00831006
        sra     $2, $3, $4              # 00831007
        rotr    $2, $3, $4              # 00831046
        ror     $2, $3, 4               # 00231102
        rorv    $2, $3, $4              # 00831046
        slt     $2, $3, -1              # 2862ffff
        sltu    $2, $3, 4               # 2c620004
        addu    $2, $3, -32768          # 24628000
        add     $2, $3, 100             # 20620064
        subu    $2, $3, 32768           # 24628000
        sub     $2, $3, 1               # 2062ffff
        and     $2, $3, 0xff            # 306200ff
        or      $2, $3, 0x1234          # 34621234
        xor     $2, $3, 0xffff          # 3862ffff
        nor     $2, $3, 0x10            # 34620010 00401027
        slt     $2, $3, 0x12345         # 3c010001 34212345 0061102a
        sltu    $2, $3, -40000          # 3c01ffff 342163c0 0061102b
        and     $2, $3, -1              # 2401ffff 00611024
        or      $2, $3, 0x10000         # 3c010001 00611025
        addu    $2, $3, 0x10000         # 3c010001 00611021
        subu    $2, $3, -32768          # 24018000 00611023
        mul     $2, $3, 5               # 24010005 00610018 00001012
