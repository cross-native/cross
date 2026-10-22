# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# MIPS32 Release 6 instructions: three-operand multiply and divide, selects,
# FPU comparisons into an FPR and the operations that read them, fused
# multiply-add, rounding, classification, minimum and maximum, scaled and
# upper-immediate adds, byte alignment, bit reversal, SIGRIE, PC-relative
# forms, the re-encoded CLZ, CLO, LL, SC, CACHE, PREF, and JR, compact
# jumps, and the macros whose immediate forms go through $at.
        .set noreorder
        .text
        mul     $2, $4, $5              # 00851098
        muh     $2, $4, $5              # 008510d8
        mulu    $2, $4, $5              # 00851099
        muhu    $2, $4, $5              # 008510d9
        div     $2, $4, $5              # 0085109a
        mod     $2, $4, $5              # 008510da
        divu    $2, $4, $5              # 0085109b
        modu    $2, $4, $5              # 008510db
        seleqz  $2, $4, $5              # 00851035
        selnez  $2, $4, $5              # 00851037
        sel.s   $f0, $f1, $f2           # 46020810
        sel.d   $f4, $f6, $f8           # 46283110
        seleqz.s $f0, $f1, $f2          # 46020814
        seleqz.d $f0, $f1, $f2          # 46220814
        selnez.s $f3, $f5, $f7          # 460728d7
        selnez.d $f3, $f5, $f7          # 462728d7
        cmp.af.s $f0, $f2, $f4          # 46841000
        cmp.un.s $f0, $f2, $f4          # 46841001
        cmp.eq.s $f0, $f2, $f4          # 46841002
        cmp.ueq.s $f0, $f2, $f4         # 46841003
        cmp.lt.s $f0, $f2, $f4          # 46841004
        cmp.ult.s $f0, $f2, $f4         # 46841005
        cmp.le.s $f0, $f2, $f4          # 46841006
        cmp.ule.s $f0, $f2, $f4         # 46841007
        cmp.saf.s $f0, $f2, $f4         # 46841008
        cmp.sun.s $f0, $f2, $f4         # 46841009
        cmp.seq.s $f0, $f2, $f4         # 4684100a
        cmp.sueq.s $f0, $f2, $f4        # 4684100b
        cmp.slt.s $f0, $f2, $f4         # 4684100c
        cmp.sult.s $f0, $f2, $f4        # 4684100d
        cmp.sle.s $f0, $f2, $f4         # 4684100e
        cmp.sule.s $f0, $f2, $f4        # 4684100f
        cmp.or.s $f0, $f2, $f4          # 46841011
        cmp.une.s $f0, $f2, $f4         # 46841012
        cmp.ne.s $f0, $f2, $f4          # 46841013
        cmp.sor.s $f0, $f2, $f4         # 46841019
        cmp.sune.s $f0, $f2, $f4        # 4684101a
        cmp.sne.s $f0, $f2, $f4         # 4684101b
        cmp.af.d $f1, $f3, $f5          # 46a51840
        cmp.un.d $f1, $f3, $f5          # 46a51841
        cmp.eq.d $f1, $f3, $f5          # 46a51842
        cmp.ueq.d $f1, $f3, $f5         # 46a51843
        cmp.lt.d $f1, $f3, $f5          # 46a51844
        cmp.ult.d $f1, $f3, $f5         # 46a51845
        cmp.le.d $f1, $f3, $f5          # 46a51846
        cmp.ule.d $f1, $f3, $f5         # 46a51847
        cmp.saf.d $f1, $f3, $f5         # 46a51848
        cmp.sun.d $f1, $f3, $f5         # 46a51849
        cmp.seq.d $f1, $f3, $f5         # 46a5184a
        cmp.sueq.d $f1, $f3, $f5        # 46a5184b
        cmp.slt.d $f1, $f3, $f5         # 46a5184c
        cmp.sult.d $f1, $f3, $f5        # 46a5184d
        cmp.sle.d $f1, $f3, $f5         # 46a5184e
        cmp.sule.d $f1, $f3, $f5        # 46a5184f
        cmp.or.d $f1, $f3, $f5          # 46a51851
        cmp.une.d $f1, $f3, $f5         # 46a51852
        cmp.ne.d $f1, $f3, $f5          # 46a51853
        cmp.sor.d $f1, $f3, $f5         # 46a51859
        cmp.sune.d $f1, $f3, $f5        # 46a5185a
        cmp.sne.d $f1, $f3, $f5         # 46a5185b
        maddf.s $f0, $f2, $f4           # 46041018
        maddf.d $f0, $f2, $f4           # 46241018
        msubf.s $f1, $f3, $f5           # 46051859
        msubf.d $f1, $f3, $f5           # 46251859
        rint.s  $f0, $f2                # 4600101a
        rint.d  $f0, $f2                # 4620101a
        class.s $f0, $f2                # 4600101b
        class.d $f0, $f2                # 4620101b
        min.s   $f0, $f2, $f4           # 4604101c
        min.d   $f0, $f2, $f4           # 4624101c
        mina.s  $f0, $f2, $f4           # 4604101d
        mina.d  $f0, $f2, $f4           # 4624101d
        max.s   $f0, $f2, $f4           # 4604101e
        max.d   $f0, $f2, $f4           # 4624101e
        maxa.s  $f0, $f2, $f4           # 4604101f
        maxa.d  $f0, $f2, $f4           # 4624101f
        lsa     $2, $3, $4, 1           # 00641005
        lsa     $2, $3, $4, 4           # 006410c5
        aui     $2, $3, 0x1234          # 3c621234
        aui     $2, $3, 0xffff          # 3c62ffff
        align   $2, $3, $4, 0           # 7c641220
        align   $2, $3, $4, 3           # 7c6412e0
        bitswap $2, $3                  # 7c031020
        sigrie  5                       # 04170005
        auipc   $2, 0x10                # ec5e0010
        aluipc  $3, 0xffff              # ec7fffff
        addiupc $2, 8                   # ec400002
        lapc    $2, -16                 # ec47fffc
        clz     $2, $3                  # 00601050
        clo     $2, $3                  # 00601051
        ll      $2, 8($4)               # 7c820436
        ll      $2, -256($4)            # 7c828036
        ll      $2, ($4)                # 7c820036
        sc      $2, 255($4)             # 7c827fa6
        cache   1, 255($4)              # 7c817fa5
        cache   0x14, -4($5)            # 7cb4fe25
        pref    1, -4($4)               # 7c81fe35
        pref    30, 8($4)               # 7c9e0435
        jr      $31                     # 03e00009
        jr      $25                     # 03200009
        jalr    $25                     # 0320f809
        jalr    $2, $25                 # 03201009
        jic     $25, 8                  # d8190008
        jic     $25, -32768             # d8198000
        jialc   $25, 0                  # f8190000
        jrc     $31                     # d81f0000
        add     $2, $3, 5               # 24010005 00611020
        sub     $2, $3, 5               # 24010005 00611022
        teq     $4, 5                   # 24010005 00810034
        tne     $4, -3                  # 2401fffd 00810036
        tge     $4, 6                   # 24010006 00810030
        tgeu    $4, 7                   # 24010007 00810031
        tlt     $4, 8                   # 24010008 00810032
        tltu    $4, 9                   # 24010009 00810033
        teq     $4, $5                  # 00850034
        teq     $4, $5, 7               # 008501f4
        nop                             # 00000000
        sync                            # 0000000f
        sync    0x10                    # 0000040f
        syscall                         # 0000000c
        break   3                       # 0003000d
        eret                            # 42000018
        wait                            # 42000020
        ehb                             # 000000c0
        rotr    $2, $3, 4               # 00231102
        rotrv   $2, $3, $4              # 00831046
        seb     $2, $3                  # 7c031420
        seh     $2, $3                  # 7c031620
        wsbh    $2, $3                  # 7c0310a0
        ext     $2, $3, 4, 8            # 7c623900
        ins     $2, $3, 4, 8            # 7c625904
        mfhc1   $2, $f3                 # 44621800
        mthc1   $2, $f3                 # 44e21800
        add.d   $f1, $f3, $f5           # 46251840
        mov.d   $f1, $f3                # 46201846
        ldc1    $f1, 8($4)              # d4810008
        sdc1    $f1, 8($4)              # f4810008
