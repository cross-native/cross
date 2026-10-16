# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each instruction or macro is followed by the words it encodes to, in
# order; GNU as and LLVM MC produce the same words.
        .set noreorder
        .set noat
        .set mips64r2
        .text
start:
        addu $1, $2, $3                  # 00430821
        addiu $4, $5, -32768             # 24a48000
        addiu $4, $5, 32767              # 24a47fff
        daddu $6, $7, $8                 # 00e8302d
        daddiu $sp, $sp, -16             # 67bdfff0
        subu $9, $10, $11                # 014b4823
        dsubu $12, $13, $14              # 01ae602f
        and $15, $16, $17                # 02117824
        andi $18, $19, 65535             # 3272ffff
        or $20, $21, $22                 # 02b6a025
        ori $23, $24, 0x1234             # 37171234
        xor $25, $26, $27                # 035bc826
        xori $28, $29, 255               # 3bbc00ff
        nor $30, $31, $0                 # 03e0f027
        lui $2, 0xffff                   # 3c02ffff
        slt $1, $2, $3                   # 0043082a
        slti $1, $2, -1                  # 2841ffff
        sltu $1, $2, $3                  # 0043082b
        sltiu $1, $2, 4096               # 2c411000
        sll $3, $4, 31                   # 00041fc0
        srl $3, $4, 1                    # 00041842
        sra $3, $4, 7                    # 000419c3
        sllv $5, $6, $7                  # 00e62804
        srlv $5, $6, $7                  # 00e62806
        srav $5, $6, $7                  # 00e62807
        dsll $8, $9, 3                   # 000940f8
        dsrl $8, $9, 63                  # 000947fe
        dsra $8, $9, 32                  # 0009403f
        dsll32 $10, $11, 0               # 000b503c
        dsrl32 $10, $11, 31              # 000b57fe
        dsra32 $10, $11, 5               # 000b517f
        dsllv $12, $13, $14              # 01cd6014
        dsrlv $12, $13, $14              # 01cd6016
        dsrav $12, $13, $14              # 01cd6017
        rotr $2, $3, 16                  # 00231402
        rotrv $2, $3, $4                 # 00831046
        clz $2, $4                       # 70821020
        clo $5, $6                       # 70c52821
        dclz $7, $8                      # 71073824
        dclo $9, $10                     # 71494825
        wsbh $2, $4                      # 7c0410a0
        movn $2, $3, $4                  # 0064100b
        movz $2, $3, $4                  # 0064100a
        movf $2, $3, $fcc0               # 00601001
        movt $4, $5, $fcc7               # 00bd2001
        mult $6, $7                      # 00c70018
        multu $6, $7                     # 00c70019
        dmult $8, $9                     # 0109001c
        dmultu $8, $9                    # 0109001d
        div $zero, $10, $11              # 014b001a
        divu $zero, $10, $11             # 014b001b
        ddiv $zero, $12, $13             # 018d001e
        ddivu $0, $12, $13               # 018d001f
        mfhi $16                         # 00008010
        mflo $17                         # 00008812
        mthi $18                         # 02400011
        mtlo $19                         # 02600013
        lb $2, -4($sp)                   # 83a2fffc
        lbu $3, 0($4)                    # 90830000
        lh $5, 2($6)                     # 84c50002
        lhu $7, 4($8)                    # 95070004
        lw $9, 8($10)                    # 8d490008
        lwu $11, 12($12)                 # 9d8b000c
        ld $13, 16($14)                  # ddcd0010
        lwl $15, 3($16)                  # 8a0f0003
        lwr $15, 0($16)                  # 9a0f0000
        ldl $17, 7($18)                  # 6a510007
        ldr $17, 0($18)                  # 6e510000
        lwc1 $f1, 4($19)                 # c6610004
        ldc1 $f2, 8($20)                 # d6820008
        ll $21, 0($22)                   # c2d50000
        lld $23, 0($24)                  # d3170000
        sb $2, 1($3)                     # a0620001
        sh $4, 2($5)                     # a4a40002
        sw $6, 4($7)                     # ace60004
        sd $8, 8($9)                     # fd280008
        swl $10, 3($11)                  # a96a0003
        swr $10, 0($11)                  # b96a0000
        sdl $12, 7($13)                  # b1ac0007
        sdr $12, 0($13)                  # b5ac0000
        swc1 $f3, 4($14)                 # e5c30004
        sdc1 $f4, 8($15)                 # f5e40008
        sc $16, 0($17)                   # e2300000
        scd $18, 0($19)                  # f2720000
        lw $2, ($3)                      # 8c620000
        lw $2, 0x100                     # 8c020100
        mfc1 $2, $f4                     # 44022000
        mtc1 $3, $f5                     # 44832800
        dmfc1 $4, $f6                    # 44243000
        dmtc1 $5, $f7                    # 44a53800
        mfhc1 $6, $f8                    # 44664000
        mthc1 $7, $f9                    # 44e74800
        cfc1 $8, $31                     # 4448f800
        ctc1 $9, $31                     # 44c9f800
        add.s $f0, $f1, $f2              # 46020800
        add.d $f2, $f4, $f6              # 46262080
        sub.s $f0, $f1, $f2              # 46020801
        sub.d $f2, $f4, $f6              # 46262081
        mul.s $f0, $f1, $f2              # 46020802
        mul.d $f2, $f4, $f6              # 46262082
        div.s $f0, $f1, $f2              # 46020803
        div.d $f2, $f4, $f6              # 46262083
        sqrt.s $f3, $f5                  # 460028c4
        sqrt.d $f4, $f6                  # 46203104
        abs.s $f3, $f5                   # 460028c5
        abs.d $f4, $f6                   # 46203105
        neg.s $f3, $f5                   # 460028c7
        neg.d $f4, $f6                   # 46203107
        mov.s $f3, $f5                   # 460028c6
        mov.d $f4, $f6                   # 46203106
        cvt.s.d $f1, $f2                 # 46201060
        cvt.d.s $f2, $f1                 # 460008a1
        cvt.s.w $f3, $f4                 # 468020e0
        cvt.d.w $f4, $f5                 # 46802921
        cvt.s.l $f5, $f6                 # 46a03160
        cvt.d.l $f6, $f8                 # 46a041a1
        trunc.w.s $f7, $f8               # 460041cd
        trunc.w.d $f9, $f10              # 4620524d
        trunc.l.s $f10, $f11             # 46005a89
        trunc.l.d $f12, $f14             # 46207309
        c.eq.s $f1, $f2                  # 46020832
        c.eq.d $f2, $f4                  # 46241032
        c.lt.s $f1, $f2                  # 4602083c
        c.lt.d $f2, $f4                  # 4624103c
        c.le.s $f1, $f2                  # 4602083e
        c.le.d $f2, $f4                  # 4624103e
        c.olt.s $f1, $f2                 # 46020834
        c.olt.d $f2, $f4                 # 46241034
        c.un.s $f1, $f2                  # 46020831
        c.un.d $f2, $f4                  # 46241031
        c.eq.s $fcc3, $f1, $f2           # 46020b32
        movn.s $f1, $f2, $3              # 46031053
        movn.d $f2, $f4, $5              # 46252093
        movz.s $f1, $f2, $3              # 46031052
        movz.d $f2, $f4, $5              # 46252092
        movt.s $f1, $f2, $fcc1           # 46051051
        movt.d $f2, $f4, $fcc2           # 46292091
        movf.s $f1, $f2, $fcc0           # 46001051
        movf.d $f2, $f4, $fcc5           # 46342091
        nop                              # 00000000
        ehb                              # 000000c0
        ssnop                            # 00000040
        sync                             # 0000000f
        sync 4                           # 0000010f
        break                            # 0000000d
        break 7                          # 0007000d
        break 7, 3                       # 000700cd
        syscall                          # 0000000c
        syscall 0x12345                  # 0048d14c
        eret                             # 42000018
        wait                             # 42000020
        cache 25, 12($3)                 # bc79000c
        mfc0 $8, $12                     # 40086000
        mtc0 $8, $12                     # 40886000
        mfc0 $9, $16, 1                  # 40098001
        dmfc0 $10, $14                   # 402a7000
        dmtc0 $10, $14                   # 40aa7000
        tlbp                             # 42000008
        tlbr                             # 42000001
        tlbwi                            # 42000002
        tlbwr                            # 42000006
        add $2, $3, $4                   # 00641020
        addi $2, $3, -5                  # 2062fffb
        sub $2, $3, $4                   # 00641022
        dadd $2, $3, $4                  # 0064102c
        daddi $2, $3, 5                  # 60620005
        dsub $2, $3, $4                  # 0064102e
        negu $2, $3                      # 00031023
        neg $4, $5                       # 00052022
        not $6, $7                       # 00e03027
        move $2, $4                      # 00801025
        li $2, 0                         # 24020000
        li $3, -1                        # 2403ffff
        li $4, 0x8000                    # 34048000
        li $5, 0xffff                    # 3405ffff
        li $6, 0x10000                   # 3c060001
        li $7, 0x12345678                # 3c071234 34e75678
        li $8, 0xffff8000                # 24088000
        li $9, 0x80000000                # 3c098000
        dli $10, 0x0000000100000000      # 340a8000 000a5478
        dli $11, 0x0000001700000017      # 240b0017 000b583c 356b0017
        dli $12, 0xffffffff              # 3c0cffff 000c603e
        dli $13, 0x0fedcba987654321      # 3c0d0fed 35adcba9 000d6c38 35ad8765 000d6c38 35ad4321
        dli $14, 0x0001000000000000      # 340e8000 000e707c
        dli $15, -2                      # 240ffffe
        jr $31                           # 03e00008
        jalr $25                         # 0320f809
        jalr $2, $25                     # 03201009
back:
        beq $2, $3, back                 # 1043ffff
        bne $2, $0, forward              # 14400018
        beql $2, $3, back                # 5043fffd
        bnel $2, $3, forward             # 54430016
        beqz $4, back                    # 1080fffb
        bnez $4, forward                 # 14800014
        beqzl $4, back                   # 5080fff9
        bnezl $4, forward                # 54800012
        bltz $5, back                    # 04a0fff7
        bgez $5, forward                 # 04a10010
        blez $5, back                    # 18a0fff5
        bgtz $5, forward                 # 1ca0000e
        bltzl $5, back                   # 04a2fff3
        bgezl $5, forward                # 04a3000c
        blezl $5, back                   # 58a0fff1
        bgtzl $5, forward                # 5ca0000a
        bltzal $6, back                  # 04d0ffef
        bgezal $6, forward               # 04d10008
        bc1t back                        # 4501ffed
        bc1f forward                     # 45000006
        bc1tl back                       # 4503ffeb
        bc1fl forward                    # 45020004
        bc1t $fcc2, back                 # 4509ffe9
        b back                           # 1000ffe8
        bal forward                      # 04110001
        nop                              # 00000000
forward:
        nop                              # 00000000
