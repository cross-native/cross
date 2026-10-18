# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Every form of the shipped MIPS instruction table, with the words of its
# encoding in the comments, under the ISA level that has it.
        .set noreorder
        .text
        .set push
        .set mips1
s0:
        addu    $2, $3, $4                      # 00641021
1:
        .set pop
        .set push
        .set mips1
s1:
        addu    $3, $4, $5                      # 00851821
1:
        .set pop
        .set push
        .set mips1
s2:
        addu    $4, $5, $6                      # 00a62021
1:
        .set pop
        .set push
        .set mips1
s3:
        addu    $2, $3, 12                      # 2462000c
1:
        .set pop
        .set push
        .set mips1
s4:
        addu    $3, $4, 0x12345                 # 3c010001 34212345 00811821
1:
        .set pop
        .set push
        .set mips1
s5:
        addu    $4, $5, 12                      # 24a4000c
1:
        .set pop
        .set push
        .set mips1
s6:
        addiu   $2, $3, -5                      # 2462fffb
1:
        .set pop
        .set push
        .set mips1
s7:
        addiu   $3, $4, 3                       # 24830003
1:
        .set pop
        .set push
        .set mips1
s8:
        addiu   $4, $5, %lo(external)           # 24a40000
1:
        .set pop
        .set push
        .set mips3
s9:
        daddu   $2, $3, $4                      # 0064102d
1:
        .set pop
        .set push
        .set mips3
s10:
        daddu   $3, $4, $5                      # 0085182d
1:
        .set pop
        .set push
        .set mips3
s11:
        daddu   $4, $5, $6                      # 00a6202d
1:
        .set pop
        .set push
        .set mips3
s12:
        daddu   $2, $3, 12                      # 6462000c
1:
        .set pop
        .set push
        .set mips3
s13:
        daddu   $3, $4, 0x12345                 # 3c010001 34212345 0081182d
1:
        .set pop
        .set push
        .set mips3
s14:
        daddu   $4, $5, 12                      # 64a4000c
1:
        .set pop
        .set push
        .set mips3
s15:
        daddiu  $2, $3, -5                      # 6462fffb
1:
        .set pop
        .set push
        .set mips3
s16:
        daddiu  $3, $4, 3                       # 64830003
1:
        .set pop
        .set push
        .set mips3
s17:
        daddiu  $4, $5, %lo(external)           # 64a40000
1:
        .set pop
        .set push
        .set mips1
s18:
        subu    $2, $3, $4                      # 00641023
1:
        .set pop
        .set push
        .set mips1
s19:
        subu    $3, $4, $5                      # 00851823
1:
        .set pop
        .set push
        .set mips1
s20:
        subu    $4, $5, $6                      # 00a62023
1:
        .set pop
        .set push
        .set mips1
s21:
        subu    $2, $3, 12                      # 2462fff4
1:
        .set pop
        .set push
        .set mips1
s22:
        subu    $3, $4, 0x12345                 # 3c010001 34212345 00811823
1:
        .set pop
        .set push
        .set mips1
s23:
        subu    $4, $5, 12                      # 24a4fff4
1:
        .set pop
        .set push
        .set mips3
s24:
        dsubu   $2, $3, $4                      # 0064102f
1:
        .set pop
        .set push
        .set mips3
s25:
        dsubu   $3, $4, $5                      # 0085182f
1:
        .set pop
        .set push
        .set mips3
s26:
        dsubu   $4, $5, $6                      # 00a6202f
1:
        .set pop
        .set push
        .set mips3
s27:
        dsubu   $2, $3, 12                      # 6462fff4
1:
        .set pop
        .set push
        .set mips3
s28:
        dsubu   $3, $4, 0x12345                 # 3c010001 34212345 0081182f
1:
        .set pop
        .set push
        .set mips3
s29:
        dsubu   $4, $5, 12                      # 64a4fff4
1:
        .set pop
        .set push
        .set mips1
s30:
        and     $2, $3, $4                      # 00641024
1:
        .set pop
        .set push
        .set mips1
s31:
        and     $3, $4, $5                      # 00851824
1:
        .set pop
        .set push
        .set mips1
s32:
        and     $4, $5, $6                      # 00a62024
1:
        .set pop
        .set push
        .set mips1
s33:
        and     $2, $3, 12                      # 3062000c
1:
        .set pop
        .set push
        .set mips1
s34:
        and     $3, $4, 0x12345                 # 3c010001 34212345 00811824
1:
        .set pop
        .set push
        .set mips1
s35:
        and     $4, $5, 12                      # 30a4000c
1:
        .set pop
        .set push
        .set mips1
s36:
        andi    $2, $3, 5                       # 30620005
1:
        .set pop
        .set push
        .set mips1
s37:
        andi    $3, $4, 1                       # 30830001
1:
        .set pop
        .set push
        .set mips1
s38:
        andi    $4, $5, %lo(external)           # 30a40000
1:
        .set pop
        .set push
        .set mips1
s39:
        or      $2, $3, $4                      # 00641025
1:
        .set pop
        .set push
        .set mips1
s40:
        or      $3, $4, $5                      # 00851825
1:
        .set pop
        .set push
        .set mips1
s41:
        or      $4, $5, $6                      # 00a62025
1:
        .set pop
        .set push
        .set mips1
s42:
        or      $2, $3, 12                      # 3462000c
1:
        .set pop
        .set push
        .set mips1
s43:
        or      $3, $4, 0x12345                 # 3c010001 34212345 00811825
1:
        .set pop
        .set push
        .set mips1
s44:
        or      $4, $5, 12                      # 34a4000c
1:
        .set pop
        .set push
        .set mips1
s45:
        ori     $2, $3, 5                       # 34620005
1:
        .set pop
        .set push
        .set mips1
s46:
        ori     $3, $4, 1                       # 34830001
1:
        .set pop
        .set push
        .set mips1
s47:
        ori     $4, $5, %lo(external)           # 34a40000
1:
        .set pop
        .set push
        .set mips1
s48:
        xor     $2, $3, $4                      # 00641026
1:
        .set pop
        .set push
        .set mips1
s49:
        xor     $3, $4, $5                      # 00851826
1:
        .set pop
        .set push
        .set mips1
s50:
        xor     $4, $5, $6                      # 00a62026
1:
        .set pop
        .set push
        .set mips1
s51:
        xor     $2, $3, 12                      # 3862000c
1:
        .set pop
        .set push
        .set mips1
s52:
        xor     $3, $4, 0x12345                 # 3c010001 34212345 00811826
1:
        .set pop
        .set push
        .set mips1
s53:
        xor     $4, $5, 12                      # 38a4000c
1:
        .set pop
        .set push
        .set mips1
s54:
        xori    $2, $3, 5                       # 38620005
1:
        .set pop
        .set push
        .set mips1
s55:
        xori    $3, $4, 1                       # 38830001
1:
        .set pop
        .set push
        .set mips1
s56:
        xori    $4, $5, %lo(external)           # 38a40000
1:
        .set pop
        .set push
        .set mips1
s57:
        nor     $2, $3, $4                      # 00641027
1:
        .set pop
        .set push
        .set mips1
s58:
        nor     $3, $4, $5                      # 00851827
1:
        .set pop
        .set push
        .set mips1
s59:
        nor     $4, $5, $6                      # 00a62027
1:
        .set pop
        .set push
        .set mips1
s60:
        nor     $2, $3, 12                      # 3462000c 00401027
1:
        .set pop
        .set push
        .set mips1
s61:
        nor     $3, $4, 0x12345                 # 3c010001 34212345 00811827
1:
        .set pop
        .set push
        .set mips1
s62:
        nor     $4, $5, 12                      # 34a4000c 00802027
1:
        .set pop
        .set push
        .set mips1
s63:
        lui     $2, 5                           # 3c020005
1:
        .set pop
        .set push
        .set mips1
s64:
        lui     $3, 1                           # 3c030001
1:
        .set pop
        .set push
        .set mips1
s65:
        lui     $4, %lo(external)               # 3c040000
1:
        .set pop
        .set push
        .set mips1
s66:
        li      $2, 0x12345678                  # 3c021234 34425678
1:
        .set pop
        .set push
        .set mips1
s67:
        li      $3, -5                          # 2403fffb
1:
        .set pop
        .set push
        .set mips1
s68:
        li      $4, 0xffff                      # 3404ffff
1:
        .set pop
        .set push
        .set mips1
s69:
        move    $2, $3                          # 00601025
1:
        .set pop
        .set push
        .set mips1
s70:
        move    $3, $4                          # 00801825
1:
        .set pop
        .set push
        .set mips1
s71:
        move    $4, $5                          # 00a02025
1:
        .set pop
        .set push
        .set mips1
s72:
        slt     $2, $3, $4                      # 0064102a
1:
        .set pop
        .set push
        .set mips1
s73:
        slt     $3, $4, $5                      # 0085182a
1:
        .set pop
        .set push
        .set mips1
s74:
        slt     $4, $5, $6                      # 00a6202a
1:
        .set pop
        .set push
        .set mips1
s75:
        slt     $2, $3, 12                      # 2862000c
1:
        .set pop
        .set push
        .set mips1
s76:
        slt     $3, $4, 0x12345                 # 3c010001 34212345 0081182a
1:
        .set pop
        .set push
        .set mips1
s77:
        slt     $4, $5, 12                      # 28a4000c
1:
        .set pop
        .set push
        .set mips1
s78:
        slti    $2, $3, -5                      # 2862fffb
1:
        .set pop
        .set push
        .set mips1
s79:
        slti    $3, $4, 3                       # 28830003
1:
        .set pop
        .set push
        .set mips1
s80:
        slti    $4, $5, %lo(external)           # 28a40000
1:
        .set pop
        .set push
        .set mips1
s81:
        sltu    $2, $3, $4                      # 0064102b
1:
        .set pop
        .set push
        .set mips1
s82:
        sltu    $3, $4, $5                      # 0085182b
1:
        .set pop
        .set push
        .set mips1
s83:
        sltu    $4, $5, $6                      # 00a6202b
1:
        .set pop
        .set push
        .set mips1
s84:
        sltu    $2, $3, 12                      # 2c62000c
1:
        .set pop
        .set push
        .set mips1
s85:
        sltu    $3, $4, 0x12345                 # 3c010001 34212345 0081182b
1:
        .set pop
        .set push
        .set mips1
s86:
        sltu    $4, $5, 12                      # 2ca4000c
1:
        .set pop
        .set push
        .set mips1
s87:
        sltiu   $2, $3, -5                      # 2c62fffb
1:
        .set pop
        .set push
        .set mips1
s88:
        sltiu   $3, $4, 3                       # 2c830003
1:
        .set pop
        .set push
        .set mips1
s89:
        sltiu   $4, $5, %lo(external)           # 2ca40000
1:
        .set pop
        .set push
        .set mips1
s90:
        sll     $2, $3, 5                       # 00031140
1:
        .set pop
        .set push
        .set mips1
s91:
        sll     $3, $4, 1                       # 00041840
1:
        .set pop
        .set push
        .set mips1
s92:
        sll     $4, $5, 31                      # 000527c0
1:
        .set pop
        .set push
        .set mips1
s93:
        sll     $2, $3, $4                      # 00831004
1:
        .set pop
        .set push
        .set mips1
s94:
        sll     $3, $4, $5                      # 00a41804
1:
        .set pop
        .set push
        .set mips1
s95:
        sll     $4, $5, $6                      # 00c52004
1:
        .set pop
        .set push
        .set mips1
s96:
        srl     $2, $3, 5                       # 00031142
1:
        .set pop
        .set push
        .set mips1
s97:
        srl     $3, $4, 1                       # 00041842
1:
        .set pop
        .set push
        .set mips1
s98:
        srl     $4, $5, 31                      # 000527c2
1:
        .set pop
        .set push
        .set mips1
s99:
        srl     $2, $3, $4                      # 00831006
1:
        .set pop
        .set push
        .set mips1
s100:
        srl     $3, $4, $5                      # 00a41806
1:
        .set pop
        .set push
        .set mips1
s101:
        srl     $4, $5, $6                      # 00c52006
1:
        .set pop
        .set push
        .set mips1
s102:
        sra     $2, $3, 5                       # 00031143
1:
        .set pop
        .set push
        .set mips1
s103:
        sra     $3, $4, 1                       # 00041843
1:
        .set pop
        .set push
        .set mips1
s104:
        sra     $4, $5, 31                      # 000527c3
1:
        .set pop
        .set push
        .set mips1
s105:
        sra     $2, $3, $4                      # 00831007
1:
        .set pop
        .set push
        .set mips1
s106:
        sra     $3, $4, $5                      # 00a41807
1:
        .set pop
        .set push
        .set mips1
s107:
        sra     $4, $5, $6                      # 00c52007
1:
        .set pop
        .set push
        .set mips1
s108:
        sllv    $2, $3, $4                      # 00831004
1:
        .set pop
        .set push
        .set mips1
s109:
        sllv    $3, $4, $5                      # 00a41804
1:
        .set pop
        .set push
        .set mips1
s110:
        sllv    $4, $5, $6                      # 00c52004
1:
        .set pop
        .set push
        .set mips1
s111:
        srlv    $2, $3, $4                      # 00831006
1:
        .set pop
        .set push
        .set mips1
s112:
        srlv    $3, $4, $5                      # 00a41806
1:
        .set pop
        .set push
        .set mips1
s113:
        srlv    $4, $5, $6                      # 00c52006
1:
        .set pop
        .set push
        .set mips1
s114:
        srav    $2, $3, $4                      # 00831007
1:
        .set pop
        .set push
        .set mips1
s115:
        srav    $3, $4, $5                      # 00a41807
1:
        .set pop
        .set push
        .set mips1
s116:
        srav    $4, $5, $6                      # 00c52007
1:
        .set pop
        .set push
        .set mips3
s117:
        dsll    $2, $3, 5                       # 00031178
1:
        .set pop
        .set push
        .set mips3
s118:
        dsll    $3, $4, 1                       # 00041878
1:
        .set pop
        .set push
        .set mips3
s119:
        dsll    $4, $5, 63                      # 000527fc
1:
        .set pop
        .set push
        .set mips3
s120:
        dsll    $2, $3, $4                      # 00831014
1:
        .set pop
        .set push
        .set mips3
s121:
        dsll    $3, $4, $5                      # 00a41814
1:
        .set pop
        .set push
        .set mips3
s122:
        dsll    $4, $5, $6                      # 00c52014
1:
        .set pop
        .set push
        .set mips3
s123:
        dsrl    $2, $3, 5                       # 0003117a
1:
        .set pop
        .set push
        .set mips3
s124:
        dsrl    $3, $4, 1                       # 0004187a
1:
        .set pop
        .set push
        .set mips3
s125:
        dsrl    $4, $5, 63                      # 000527fe
1:
        .set pop
        .set push
        .set mips3
s126:
        dsrl    $2, $3, $4                      # 00831016
1:
        .set pop
        .set push
        .set mips3
s127:
        dsrl    $3, $4, $5                      # 00a41816
1:
        .set pop
        .set push
        .set mips3
s128:
        dsrl    $4, $5, $6                      # 00c52016
1:
        .set pop
        .set push
        .set mips3
s129:
        dsra    $2, $3, 5                       # 0003117b
1:
        .set pop
        .set push
        .set mips3
s130:
        dsra    $3, $4, 1                       # 0004187b
1:
        .set pop
        .set push
        .set mips3
s131:
        dsra    $4, $5, 63                      # 000527ff
1:
        .set pop
        .set push
        .set mips3
s132:
        dsra    $2, $3, $4                      # 00831017
1:
        .set pop
        .set push
        .set mips3
s133:
        dsra    $3, $4, $5                      # 00a41817
1:
        .set pop
        .set push
        .set mips3
s134:
        dsra    $4, $5, $6                      # 00c52017
1:
        .set pop
        .set push
        .set mips3
s135:
        dsll32  $2, $3, 5                       # 0003117c
1:
        .set pop
        .set push
        .set mips3
s136:
        dsll32  $3, $4, 1                       # 0004187c
1:
        .set pop
        .set push
        .set mips3
s137:
        dsll32  $4, $5, 31                      # 000527fc
1:
        .set pop
        .set push
        .set mips3
s138:
        dsrl32  $2, $3, 5                       # 0003117e
1:
        .set pop
        .set push
        .set mips3
s139:
        dsrl32  $3, $4, 1                       # 0004187e
1:
        .set pop
        .set push
        .set mips3
s140:
        dsrl32  $4, $5, 31                      # 000527fe
1:
        .set pop
        .set push
        .set mips3
s141:
        dsra32  $2, $3, 5                       # 0003117f
1:
        .set pop
        .set push
        .set mips3
s142:
        dsra32  $3, $4, 1                       # 0004187f
1:
        .set pop
        .set push
        .set mips3
s143:
        dsra32  $4, $5, 31                      # 000527ff
1:
        .set pop
        .set push
        .set mips3
s144:
        dsllv   $2, $3, $4                      # 00831014
1:
        .set pop
        .set push
        .set mips3
s145:
        dsllv   $3, $4, $5                      # 00a41814
1:
        .set pop
        .set push
        .set mips3
s146:
        dsllv   $4, $5, $6                      # 00c52014
1:
        .set pop
        .set push
        .set mips3
s147:
        dsrlv   $2, $3, $4                      # 00831016
1:
        .set pop
        .set push
        .set mips3
s148:
        dsrlv   $3, $4, $5                      # 00a41816
1:
        .set pop
        .set push
        .set mips3
s149:
        dsrlv   $4, $5, $6                      # 00c52016
1:
        .set pop
        .set push
        .set mips3
s150:
        dsrav   $2, $3, $4                      # 00831017
1:
        .set pop
        .set push
        .set mips3
s151:
        dsrav   $3, $4, $5                      # 00a41817
1:
        .set pop
        .set push
        .set mips3
s152:
        dsrav   $4, $5, $6                      # 00c52017
1:
        .set pop
        .set push
        .set mips32r2
s153:
        rotrv   $2, $3, $4                      # 00831046
1:
        .set pop
        .set push
        .set mips32r2
s154:
        rotrv   $3, $4, $5                      # 00a41846
1:
        .set pop
        .set push
        .set mips32r2
s155:
        rotrv   $4, $5, $6                      # 00c52046
1:
        .set pop
        .set push
        .set mips32r2
s156:
        rorv    $2, $3, $4                      # 00831046
1:
        .set pop
        .set push
        .set mips32r2
s157:
        rorv    $3, $4, $5                      # 00a41846
1:
        .set pop
        .set push
        .set mips32r2
s158:
        rorv    $4, $5, $6                      # 00c52046
1:
        .set pop
        .set push
        .set mips32r2
s159:
        rotr    $2, $3, 5                       # 00231142
1:
        .set pop
        .set push
        .set mips32r2
s160:
        rotr    $3, $4, 1                       # 00241842
1:
        .set pop
        .set push
        .set mips32r2
s161:
        rotr    $4, $5, 31                      # 002527c2
1:
        .set pop
        .set push
        .set mips32r2
s162:
        rotr    $2, $3, $4                      # 00831046
1:
        .set pop
        .set push
        .set mips32r2
s163:
        rotr    $3, $4, $5                      # 00a41846
1:
        .set pop
        .set push
        .set mips32r2
s164:
        rotr    $4, $5, $6                      # 00c52046
1:
        .set pop
        .set push
        .set mips32r2
s165:
        ror     $2, $3, 5                       # 00231142
1:
        .set pop
        .set push
        .set mips32r2
s166:
        ror     $3, $4, 1                       # 00241842
1:
        .set pop
        .set push
        .set mips32r2
s167:
        ror     $4, $5, 31                      # 002527c2
1:
        .set pop
        .set push
        .set mips32r2
s168:
        ror     $2, $3, $4                      # 00831046
1:
        .set pop
        .set push
        .set mips32r2
s169:
        ror     $3, $4, $5                      # 00a41846
1:
        .set pop
        .set push
        .set mips32r2
s170:
        ror     $4, $5, $6                      # 00c52046
1:
        .set pop
        .set push
        .set mips32
s171:
        clz     $2, $3                          # 70621020
1:
        .set pop
        .set push
        .set mips32
s172:
        clz     $3, $4                          # 70831820
1:
        .set pop
        .set push
        .set mips32
s173:
        clz     $4, $5                          # 70a42020
1:
        .set pop
        .set push
        .set arch=allegrex
s174:
        clz     $2, $3                          # 00601016
1:
        .set pop
        .set push
        .set arch=allegrex
s175:
        clz     $3, $4                          # 00801816
1:
        .set pop
        .set push
        .set arch=allegrex
s176:
        clz     $4, $5                          # 00a02016
1:
        .set pop
        .set push
        .set mips32
s177:
        clo     $2, $3                          # 70621021
1:
        .set pop
        .set push
        .set mips32
s178:
        clo     $3, $4                          # 70831821
1:
        .set pop
        .set push
        .set mips32
s179:
        clo     $4, $5                          # 70a42021
1:
        .set pop
        .set push
        .set arch=allegrex
s180:
        clo     $2, $3                          # 00601017
1:
        .set pop
        .set push
        .set arch=allegrex
s181:
        clo     $3, $4                          # 00801817
1:
        .set pop
        .set push
        .set arch=allegrex
s182:
        clo     $4, $5                          # 00a02017
1:
        .set pop
        .set push
        .set mips64
s183:
        dclz    $2, $3                          # 70621024
1:
        .set pop
        .set push
        .set mips64
s184:
        dclz    $3, $4                          # 70831824
1:
        .set pop
        .set push
        .set mips64
s185:
        dclz    $4, $5                          # 70a42024
1:
        .set pop
        .set push
        .set mips64
s186:
        dclo    $2, $3                          # 70621025
1:
        .set pop
        .set push
        .set mips64
s187:
        dclo    $3, $4                          # 70831825
1:
        .set pop
        .set push
        .set mips64
s188:
        dclo    $4, $5                          # 70a42025
1:
        .set pop
        .set push
        .set mips32r2
s189:
        wsbh    $2, $3                          # 7c0310a0
1:
        .set pop
        .set push
        .set mips32r2
s190:
        wsbh    $3, $4                          # 7c0418a0
1:
        .set pop
        .set push
        .set mips32r2
s191:
        wsbh    $4, $5                          # 7c0520a0
1:
        .set pop
        .set push
        .set arch=allegrex
s192:
        wsbw    $2, $3                          # 7c0310e0
1:
        .set pop
        .set push
        .set arch=allegrex
s193:
        wsbw    $3, $4                          # 7c0418e0
1:
        .set pop
        .set push
        .set arch=allegrex
s194:
        wsbw    $4, $5                          # 7c0520e0
1:
        .set pop
        .set push
        .set mips4
s195:
        movn    $2, $3, $4                      # 0064100b
1:
        .set pop
        .set push
        .set mips4
s196:
        movn    $3, $4, $5                      # 0085180b
1:
        .set pop
        .set push
        .set mips4
s197:
        movn    $4, $5, $6                      # 00a6200b
1:
        .set pop
        .set push
        .set mips1
s198:
        mult    $2, $3                          # 00430018
1:
        .set pop
        .set push
        .set mips1
s199:
        mult    $3, $4                          # 00640018
1:
        .set pop
        .set push
        .set mips1
s200:
        mult    $4, $5                          # 00850018
1:
        .set pop
        .set push
        .set mips1
s201:
        multu   $2, $3                          # 00430019
1:
        .set pop
        .set push
        .set mips1
s202:
        multu   $3, $4                          # 00640019
1:
        .set pop
        .set push
        .set mips1
s203:
        multu   $4, $5                          # 00850019
1:
        .set pop
        .set push
        .set mips3
s204:
        dmult   $2, $3                          # 0043001c
1:
        .set pop
        .set push
        .set mips3
s205:
        dmult   $3, $4                          # 0064001c
1:
        .set pop
        .set push
        .set mips3
s206:
        dmult   $4, $5                          # 0085001c
1:
        .set pop
        .set push
        .set mips3
s207:
        dmultu  $2, $3                          # 0043001d
1:
        .set pop
        .set push
        .set mips3
s208:
        dmultu  $3, $4                          # 0064001d
1:
        .set pop
        .set push
        .set mips3
s209:
        dmultu  $4, $5                          # 0085001d
1:
        .set pop
        .set push
        .set mips1
s210:
        div     $zero, $3, $4                   # 0064001a
1:
        .set pop
        .set push
        .set mips1
s211:
        div     $zero, $4, $5                   # 0085001a
1:
        .set pop
        .set push
        .set mips1
s212:
        div     $zero, $5, $6                   # 00a6001a
1:
        .set pop
        .set push
        .set mips1
s213:
        divu    $zero, $3, $4                   # 0064001b
1:
        .set pop
        .set push
        .set mips1
s214:
        divu    $zero, $4, $5                   # 0085001b
1:
        .set pop
        .set push
        .set mips1
s215:
        divu    $zero, $5, $6                   # 00a6001b
1:
        .set pop
        .set push
        .set mips3
s216:
        ddiv    $zero, $3, $4                   # 0064001e
1:
        .set pop
        .set push
        .set mips3
s217:
        ddiv    $zero, $4, $5                   # 0085001e
1:
        .set pop
        .set push
        .set mips3
s218:
        ddiv    $zero, $5, $6                   # 00a6001e
1:
        .set pop
        .set push
        .set mips3
s219:
        ddivu   $zero, $3, $4                   # 0064001f
1:
        .set pop
        .set push
        .set mips3
s220:
        ddivu   $zero, $4, $5                   # 0085001f
1:
        .set pop
        .set push
        .set mips3
s221:
        ddivu   $zero, $5, $6                   # 00a6001f
1:
        .set pop
        .set push
        .set mips1
s222:
        mfhi    $2                              # 00001010
1:
        .set pop
        .set push
        .set mips1
s223:
        mfhi    $3                              # 00001810
1:
        .set pop
        .set push
        .set mips1
s224:
        mfhi    $4                              # 00002010
1:
        .set pop
        .set push
        .set mips1
s225:
        mflo    $2                              # 00001012
1:
        .set pop
        .set push
        .set mips1
s226:
        mflo    $3                              # 00001812
1:
        .set pop
        .set push
        .set mips1
s227:
        mflo    $4                              # 00002012
1:
        .set pop
        .set push
        .set mips1
s228:
        mthi    $2                              # 00400011
1:
        .set pop
        .set push
        .set mips1
s229:
        mthi    $3                              # 00600011
1:
        .set pop
        .set push
        .set mips1
s230:
        mthi    $4                              # 00800011
1:
        .set pop
        .set push
        .set mips1
s231:
        mtlo    $2                              # 00400013
1:
        .set pop
        .set push
        .set mips1
s232:
        mtlo    $3                              # 00600013
1:
        .set pop
        .set push
        .set mips1
s233:
        mtlo    $4                              # 00800013
1:
        .set pop
        .set push
        .set mips1
s234:
        lb      $2, 8($4)                       # 80820008
1:
        .set pop
        .set push
        .set mips1
s235:
        lb      $3, ($5)                        # 80a30000
1:
        .set pop
        .set push
        .set mips1
s236:
        lb      $4, %lo(external)($6)           # 80c40000
1:
        .set pop
        .set push
        .set mips1
s237:
        lbu     $2, 8($4)                       # 90820008
1:
        .set pop
        .set push
        .set mips1
s238:
        lbu     $3, ($5)                        # 90a30000
1:
        .set pop
        .set push
        .set mips1
s239:
        lbu     $4, %lo(external)($6)           # 90c40000
1:
        .set pop
        .set push
        .set mips1
s240:
        lh      $2, 8($4)                       # 84820008
1:
        .set pop
        .set push
        .set mips1
s241:
        lh      $3, ($5)                        # 84a30000
1:
        .set pop
        .set push
        .set mips1
s242:
        lh      $4, %lo(external)($6)           # 84c40000
1:
        .set pop
        .set push
        .set mips1
s243:
        lhu     $2, 8($4)                       # 94820008
1:
        .set pop
        .set push
        .set mips1
s244:
        lhu     $3, ($5)                        # 94a30000
1:
        .set pop
        .set push
        .set mips1
s245:
        lhu     $4, %lo(external)($6)           # 94c40000
1:
        .set pop
        .set push
        .set mips1
s246:
        lw      $2, 8($4)                       # 8c820008
1:
        .set pop
        .set push
        .set mips1
s247:
        lw      $3, ($5)                        # 8ca30000
1:
        .set pop
        .set push
        .set mips1
s248:
        lw      $4, %lo(external)($6)           # 8cc40000
1:
        .set pop
        .set push
        .set mips3
s249:
        lwu     $2, 8($4)                       # 9c820008
1:
        .set pop
        .set push
        .set mips3
s250:
        lwu     $3, ($5)                        # 9ca30000
1:
        .set pop
        .set push
        .set mips3
s251:
        lwu     $4, %lo(external)($6)           # 9cc40000
1:
        .set pop
        .set push
        .set mips3
s252:
        ld      $2, 8($4)                       # dc820008
1:
        .set pop
        .set push
        .set mips3
s253:
        ld      $3, ($5)                        # dca30000
1:
        .set pop
        .set push
        .set mips3
s254:
        ld      $4, %lo(external)($6)           # dcc40000
1:
        .set pop
        .set push
        .set mips1
s255:
        lwl     $2, 8($4)                       # 88820008
1:
        .set pop
        .set push
        .set mips1
s256:
        lwl     $3, ($5)                        # 88a30000
1:
        .set pop
        .set push
        .set mips1
s257:
        lwl     $4, %lo(external)($6)           # 88c40000
1:
        .set pop
        .set push
        .set mips1
s258:
        lwr     $2, 8($4)                       # 98820008
1:
        .set pop
        .set push
        .set mips1
s259:
        lwr     $3, ($5)                        # 98a30000
1:
        .set pop
        .set push
        .set mips1
s260:
        lwr     $4, %lo(external)($6)           # 98c40000
1:
        .set pop
        .set push
        .set mips3
s261:
        ldl     $2, 8($4)                       # 68820008
1:
        .set pop
        .set push
        .set mips3
s262:
        ldl     $3, ($5)                        # 68a30000
1:
        .set pop
        .set push
        .set mips3
s263:
        ldl     $4, %lo(external)($6)           # 68c40000
1:
        .set pop
        .set push
        .set mips3
s264:
        ldr     $2, 8($4)                       # 6c820008
1:
        .set pop
        .set push
        .set mips3
s265:
        ldr     $3, ($5)                        # 6ca30000
1:
        .set pop
        .set push
        .set mips3
s266:
        ldr     $4, %lo(external)($6)           # 6cc40000
1:
        .set pop
        .set push
        .set mips1
s267:
        lwc1    $f2, 8($4)                      # c4820008
1:
        .set pop
        .set push
        .set mips1
s268:
        lwc1    $f4, ($5)                       # c4a40000
1:
        .set pop
        .set push
        .set mips1
s269:
        lwc1    $f6, %lo(external)($6)          # c4c60000
1:
        .set pop
        .set push
        .set mips2
s270:
        ldc1    $f2, 8($4)                      # d4820008
1:
        .set pop
        .set push
        .set mips2
s271:
        ldc1    $f4, ($5)                       # d4a40000
1:
        .set pop
        .set push
        .set mips2
s272:
        ldc1    $f6, %lo(external)($6)          # d4c60000
1:
        .set pop
        .set push
        .set mips2
s273:
        ll      $2, 8($4)                       # c0820008
1:
        .set pop
        .set push
        .set mips2
s274:
        ll      $3, ($5)                        # c0a30000
1:
        .set pop
        .set push
        .set mips2
s275:
        ll      $4, %lo(external)($6)           # c0c40000
1:
        .set pop
        .set push
        .set mips3
s276:
        lld     $2, 8($4)                       # d0820008
1:
        .set pop
        .set push
        .set mips3
s277:
        lld     $3, ($5)                        # d0a30000
1:
        .set pop
        .set push
        .set mips3
s278:
        lld     $4, %lo(external)($6)           # d0c40000
1:
        .set pop
        .set push
        .set mips1
s279:
        sb      $2, 8($4)                       # a0820008
1:
        .set pop
        .set push
        .set mips1
s280:
        sb      $3, ($5)                        # a0a30000
1:
        .set pop
        .set push
        .set mips1
s281:
        sb      $4, %lo(external)($6)           # a0c40000
1:
        .set pop
        .set push
        .set mips1
s282:
        sh      $2, 8($4)                       # a4820008
1:
        .set pop
        .set push
        .set mips1
s283:
        sh      $3, ($5)                        # a4a30000
1:
        .set pop
        .set push
        .set mips1
s284:
        sh      $4, %lo(external)($6)           # a4c40000
1:
        .set pop
        .set push
        .set mips1
s285:
        sw      $2, 8($4)                       # ac820008
1:
        .set pop
        .set push
        .set mips1
s286:
        sw      $3, ($5)                        # aca30000
1:
        .set pop
        .set push
        .set mips1
s287:
        sw      $4, %lo(external)($6)           # acc40000
1:
        .set pop
        .set push
        .set mips3
s288:
        sd      $2, 8($4)                       # fc820008
1:
        .set pop
        .set push
        .set mips3
s289:
        sd      $3, ($5)                        # fca30000
1:
        .set pop
        .set push
        .set mips3
s290:
        sd      $4, %lo(external)($6)           # fcc40000
1:
        .set pop
        .set push
        .set mips1
s291:
        swl     $2, 8($4)                       # a8820008
1:
        .set pop
        .set push
        .set mips1
s292:
        swl     $3, ($5)                        # a8a30000
1:
        .set pop
        .set push
        .set mips1
s293:
        swl     $4, %lo(external)($6)           # a8c40000
1:
        .set pop
        .set push
        .set mips1
s294:
        swr     $2, 8($4)                       # b8820008
1:
        .set pop
        .set push
        .set mips1
s295:
        swr     $3, ($5)                        # b8a30000
1:
        .set pop
        .set push
        .set mips1
s296:
        swr     $4, %lo(external)($6)           # b8c40000
1:
        .set pop
        .set push
        .set mips3
s297:
        sdl     $2, 8($4)                       # b0820008
1:
        .set pop
        .set push
        .set mips3
s298:
        sdl     $3, ($5)                        # b0a30000
1:
        .set pop
        .set push
        .set mips3
s299:
        sdl     $4, %lo(external)($6)           # b0c40000
1:
        .set pop
        .set push
        .set mips3
s300:
        sdr     $2, 8($4)                       # b4820008
1:
        .set pop
        .set push
        .set mips3
s301:
        sdr     $3, ($5)                        # b4a30000
1:
        .set pop
        .set push
        .set mips3
s302:
        sdr     $4, %lo(external)($6)           # b4c40000
1:
        .set pop
        .set push
        .set mips1
s303:
        swc1    $f2, 8($4)                      # e4820008
1:
        .set pop
        .set push
        .set mips1
s304:
        swc1    $f4, ($5)                       # e4a40000
1:
        .set pop
        .set push
        .set mips1
s305:
        swc1    $f6, %lo(external)($6)          # e4c60000
1:
        .set pop
        .set push
        .set mips2
s306:
        sdc1    $f2, 8($4)                      # f4820008
1:
        .set pop
        .set push
        .set mips2
s307:
        sdc1    $f4, ($5)                       # f4a40000
1:
        .set pop
        .set push
        .set mips2
s308:
        sdc1    $f6, %lo(external)($6)          # f4c60000
1:
        .set pop
        .set push
        .set mips2
s309:
        sc      $2, 8($4)                       # e0820008
1:
        .set pop
        .set push
        .set mips2
s310:
        sc      $3, ($5)                        # e0a30000
1:
        .set pop
        .set push
        .set mips2
s311:
        sc      $4, %lo(external)($6)           # e0c40000
1:
        .set pop
        .set push
        .set mips3
s312:
        scd     $2, 8($4)                       # f0820008
1:
        .set pop
        .set push
        .set mips3
s313:
        scd     $3, ($5)                        # f0a30000
1:
        .set pop
        .set push
        .set mips3
s314:
        scd     $4, %lo(external)($6)           # f0c40000
1:
        .set pop
        .set push
        .set mips1
s315:
        b       1f                              # 10000000
1:
        .set pop
        .set push
        .set mips1
s316:
        bal     1f                              # 04110000
1:
        .set pop
        .set push
        .set mips1
s317:
        beq     $2, $3, 1f                      # 10430000
1:
        .set pop
        .set push
        .set mips1
s318:
        beq     $3, $4, 1f                      # 10640000
1:
        .set pop
        .set push
        .set mips1
s319:
        beq     $4, $5, 1f                      # 10850000
1:
        .set pop
        .set push
        .set mips1
s320:
        bne     $2, $3, 1f                      # 14430000
1:
        .set pop
        .set push
        .set mips1
s321:
        bne     $3, $4, 1f                      # 14640000
1:
        .set pop
        .set push
        .set mips1
s322:
        bne     $4, $5, 1f                      # 14850000
1:
        .set pop
        .set push
        .set mips1
s323:
        beqz    $2, 1f                          # 10400000
1:
        .set pop
        .set push
        .set mips1
s324:
        beqz    $3, 1f                          # 10600000
1:
        .set pop
        .set push
        .set mips1
s325:
        beqz    $4, 1f                          # 10800000
1:
        .set pop
        .set push
        .set mips1
s326:
        bnez    $2, 1f                          # 14400000
1:
        .set pop
        .set push
        .set mips1
s327:
        bnez    $3, 1f                          # 14600000
1:
        .set pop
        .set push
        .set mips1
s328:
        bnez    $4, 1f                          # 14800000
1:
        .set pop
        .set push
        .set mips1
s329:
        bltz    $2, 1f                          # 04400000
1:
        .set pop
        .set push
        .set mips1
s330:
        bltz    $3, 1f                          # 04600000
1:
        .set pop
        .set push
        .set mips1
s331:
        bltz    $4, 1f                          # 04800000
1:
        .set pop
        .set push
        .set mips1
s332:
        bgez    $2, 1f                          # 04410000
1:
        .set pop
        .set push
        .set mips1
s333:
        bgez    $3, 1f                          # 04610000
1:
        .set pop
        .set push
        .set mips1
s334:
        bgez    $4, 1f                          # 04810000
1:
        .set pop
        .set push
        .set mips1
s335:
        blez    $2, 1f                          # 18400000
1:
        .set pop
        .set push
        .set mips1
s336:
        blez    $3, 1f                          # 18600000
1:
        .set pop
        .set push
        .set mips1
s337:
        blez    $4, 1f                          # 18800000
1:
        .set pop
        .set push
        .set mips1
s338:
        bgtz    $2, 1f                          # 1c400000
1:
        .set pop
        .set push
        .set mips1
s339:
        bgtz    $3, 1f                          # 1c600000
1:
        .set pop
        .set push
        .set mips1
s340:
        bgtz    $4, 1f                          # 1c800000
1:
        .set pop
        .set push
        .set mips1
s341:
        bltzal  $2, 1f                          # 04500000
1:
        .set pop
        .set push
        .set mips1
s342:
        bltzal  $3, 1f                          # 04700000
1:
        .set pop
        .set push
        .set mips1
s343:
        bltzal  $4, 1f                          # 04900000
1:
        .set pop
        .set push
        .set mips1
s344:
        bgezal  $2, 1f                          # 04510000
1:
        .set pop
        .set push
        .set mips1
s345:
        bgezal  $3, 1f                          # 04710000
1:
        .set pop
        .set push
        .set mips1
s346:
        bgezal  $4, 1f                          # 04910000
1:
        .set pop
        .set push
        .set mips1
s347:
        bc1t    1f                              # 45010000
1:
        .set pop
        .set push
        .set mips4
s348:
        bc1t    $fcc1, 1f                       # 45050000
1:
        .set pop
        .set push
        .set mips4
s349:
        bc1t    $fcc2, 1f                       # 45090000
1:
        .set pop
        .set push
        .set mips4
s350:
        bc1t    $fcc3, 1f                       # 450d0000
1:
        .set pop
        .set push
        .set mips1
s351:
        bc1f    1f                              # 45000000
1:
        .set pop
        .set push
        .set mips4
s352:
        bc1f    $fcc1, 1f                       # 45040000
1:
        .set pop
        .set push
        .set mips4
s353:
        bc1f    $fcc2, 1f                       # 45080000
1:
        .set pop
        .set push
        .set mips4
s354:
        bc1f    $fcc3, 1f                       # 450c0000
1:
        .set pop
        .set push
        .set mips2
s355:
        beql    $2, $3, 1f                      # 50430000
1:
        .set pop
        .set push
        .set mips2
s356:
        beql    $3, $4, 1f                      # 50640000
1:
        .set pop
        .set push
        .set mips2
s357:
        beql    $4, $5, 1f                      # 50850000
1:
        .set pop
        .set push
        .set mips2
s358:
        bnel    $2, $3, 1f                      # 54430000
1:
        .set pop
        .set push
        .set mips2
s359:
        bnel    $3, $4, 1f                      # 54640000
1:
        .set pop
        .set push
        .set mips2
s360:
        bnel    $4, $5, 1f                      # 54850000
1:
        .set pop
        .set push
        .set mips2
s361:
        beqzl   $2, 1f                          # 50400000
1:
        .set pop
        .set push
        .set mips2
s362:
        beqzl   $3, 1f                          # 50600000
1:
        .set pop
        .set push
        .set mips2
s363:
        beqzl   $4, 1f                          # 50800000
1:
        .set pop
        .set push
        .set mips2
s364:
        bnezl   $2, 1f                          # 54400000
1:
        .set pop
        .set push
        .set mips2
s365:
        bnezl   $3, 1f                          # 54600000
1:
        .set pop
        .set push
        .set mips2
s366:
        bnezl   $4, 1f                          # 54800000
1:
        .set pop
        .set push
        .set mips2
s367:
        bltzl   $2, 1f                          # 04420000
1:
        .set pop
        .set push
        .set mips2
s368:
        bltzl   $3, 1f                          # 04620000
1:
        .set pop
        .set push
        .set mips2
s369:
        bltzl   $4, 1f                          # 04820000
1:
        .set pop
        .set push
        .set mips2
s370:
        bgezl   $2, 1f                          # 04430000
1:
        .set pop
        .set push
        .set mips2
s371:
        bgezl   $3, 1f                          # 04630000
1:
        .set pop
        .set push
        .set mips2
s372:
        bgezl   $4, 1f                          # 04830000
1:
        .set pop
        .set push
        .set mips2
s373:
        blezl   $2, 1f                          # 58400000
1:
        .set pop
        .set push
        .set mips2
s374:
        blezl   $3, 1f                          # 58600000
1:
        .set pop
        .set push
        .set mips2
s375:
        blezl   $4, 1f                          # 58800000
1:
        .set pop
        .set push
        .set mips2
s376:
        bgtzl   $2, 1f                          # 5c400000
1:
        .set pop
        .set push
        .set mips2
s377:
        bgtzl   $3, 1f                          # 5c600000
1:
        .set pop
        .set push
        .set mips2
s378:
        bgtzl   $4, 1f                          # 5c800000
1:
        .set pop
        .set push
        .set mips2
s379:
        bc1tl   1f                              # 45030000
1:
        .set pop
        .set push
        .set mips4
s380:
        bc1tl   $fcc1, 1f                       # 45070000
1:
        .set pop
        .set push
        .set mips4
s381:
        bc1tl   $fcc2, 1f                       # 450b0000
1:
        .set pop
        .set push
        .set mips4
s382:
        bc1tl   $fcc3, 1f                       # 450f0000
1:
        .set pop
        .set push
        .set mips2
s383:
        bc1fl   1f                              # 45020000
1:
        .set pop
        .set push
        .set mips4
s384:
        bc1fl   $fcc1, 1f                       # 45060000
1:
        .set pop
        .set push
        .set mips4
s385:
        bc1fl   $fcc2, 1f                       # 450a0000
1:
        .set pop
        .set push
        .set mips4
s386:
        bc1fl   $fcc3, 1f                       # 450e0000
1:
        .set pop
        .set push
        .set mips1
s387:
        j       external                        # 08000000
1:
        .set pop
        .set push
        .set mips1
s388:
        jal     external                        # 0c000000
1:
        .set pop
        .set push
        .set mips1
s389:
        jr      $2                              # 00400008
1:
        .set pop
        .set push
        .set mips1
s390:
        jr      $3                              # 00600008
1:
        .set pop
        .set push
        .set mips1
s391:
        jr      $4                              # 00800008
1:
        .set pop
        .set push
        .set mips1
s392:
        jalr    $2, $3                          # 00601009
1:
        .set pop
        .set push
        .set mips1
s393:
        jalr    $3, $4                          # 00801809
1:
        .set pop
        .set push
        .set mips1
s394:
        jalr    $4, $5                          # 00a02009
1:
        .set pop
        .set push
        .set mips1
s395:
        jalr    $2                              # 0040f809
1:
        .set pop
        .set push
        .set mips1
s396:
        jalr    $3                              # 0060f809
1:
        .set pop
        .set push
        .set mips1
s397:
        jalr    $4                              # 0080f809
1:
        .set pop
        .set push
        .set mips1
s398:
        add.s   $f2, $f4, $f6                   # 46062080
1:
        .set pop
        .set push
        .set mips1
s399:
        add.s   $f4, $f6, $f8                   # 46083100
1:
        .set pop
        .set push
        .set mips1
s400:
        add.s   $f6, $f8, $f10                  # 460a4180
1:
        .set pop
        .set push
        .set mips1
s401:
        add.d   $f2, $f4, $f6                   # 46262080
1:
        .set pop
        .set push
        .set mips1
s402:
        add.d   $f4, $f6, $f8                   # 46283100
1:
        .set pop
        .set push
        .set mips1
s403:
        add.d   $f6, $f8, $f10                  # 462a4180
1:
        .set pop
        .set push
        .set mips1
s404:
        sub.s   $f2, $f4, $f6                   # 46062081
1:
        .set pop
        .set push
        .set mips1
s405:
        sub.s   $f4, $f6, $f8                   # 46083101
1:
        .set pop
        .set push
        .set mips1
s406:
        sub.s   $f6, $f8, $f10                  # 460a4181
1:
        .set pop
        .set push
        .set mips1
s407:
        sub.d   $f2, $f4, $f6                   # 46262081
1:
        .set pop
        .set push
        .set mips1
s408:
        sub.d   $f4, $f6, $f8                   # 46283101
1:
        .set pop
        .set push
        .set mips1
s409:
        sub.d   $f6, $f8, $f10                  # 462a4181
1:
        .set pop
        .set push
        .set mips1
s410:
        mul.s   $f2, $f4, $f6                   # 46062082
1:
        .set pop
        .set push
        .set mips1
s411:
        mul.s   $f4, $f6, $f8                   # 46083102
1:
        .set pop
        .set push
        .set mips1
s412:
        mul.s   $f6, $f8, $f10                  # 460a4182
1:
        .set pop
        .set push
        .set mips1
s413:
        mul.d   $f2, $f4, $f6                   # 46262082
1:
        .set pop
        .set push
        .set mips1
s414:
        mul.d   $f4, $f6, $f8                   # 46283102
1:
        .set pop
        .set push
        .set mips1
s415:
        mul.d   $f6, $f8, $f10                  # 462a4182
1:
        .set pop
        .set push
        .set mips1
s416:
        div.s   $f2, $f4, $f6                   # 46062083
1:
        .set pop
        .set push
        .set mips1
s417:
        div.s   $f4, $f6, $f8                   # 46083103
1:
        .set pop
        .set push
        .set mips1
s418:
        div.s   $f6, $f8, $f10                  # 460a4183
1:
        .set pop
        .set push
        .set mips1
s419:
        div.d   $f2, $f4, $f6                   # 46262083
1:
        .set pop
        .set push
        .set mips1
s420:
        div.d   $f4, $f6, $f8                   # 46283103
1:
        .set pop
        .set push
        .set mips1
s421:
        div.d   $f6, $f8, $f10                  # 462a4183
1:
        .set pop
        .set push
        .set mips2
s422:
        sqrt.s  $f2, $f4                        # 46002084
1:
        .set pop
        .set push
        .set mips2
s423:
        sqrt.s  $f4, $f6                        # 46003104
1:
        .set pop
        .set push
        .set mips2
s424:
        sqrt.s  $f6, $f8                        # 46004184
1:
        .set pop
        .set push
        .set mips2
s425:
        sqrt.d  $f2, $f4                        # 46202084
1:
        .set pop
        .set push
        .set mips2
s426:
        sqrt.d  $f4, $f6                        # 46203104
1:
        .set pop
        .set push
        .set mips2
s427:
        sqrt.d  $f6, $f8                        # 46204184
1:
        .set pop
        .set push
        .set mips1
s428:
        abs.s   $f2, $f4                        # 46002085
1:
        .set pop
        .set push
        .set mips1
s429:
        abs.s   $f4, $f6                        # 46003105
1:
        .set pop
        .set push
        .set mips1
s430:
        abs.s   $f6, $f8                        # 46004185
1:
        .set pop
        .set push
        .set mips1
s431:
        abs.d   $f2, $f4                        # 46202085
1:
        .set pop
        .set push
        .set mips1
s432:
        abs.d   $f4, $f6                        # 46203105
1:
        .set pop
        .set push
        .set mips1
s433:
        abs.d   $f6, $f8                        # 46204185
1:
        .set pop
        .set push
        .set mips1
s434:
        neg.s   $f2, $f4                        # 46002087
1:
        .set pop
        .set push
        .set mips1
s435:
        neg.s   $f4, $f6                        # 46003107
1:
        .set pop
        .set push
        .set mips1
s436:
        neg.s   $f6, $f8                        # 46004187
1:
        .set pop
        .set push
        .set mips1
s437:
        neg.d   $f2, $f4                        # 46202087
1:
        .set pop
        .set push
        .set mips1
s438:
        neg.d   $f4, $f6                        # 46203107
1:
        .set pop
        .set push
        .set mips1
s439:
        neg.d   $f6, $f8                        # 46204187
1:
        .set pop
        .set push
        .set mips1
s440:
        mov.s   $f2, $f4                        # 46002086
1:
        .set pop
        .set push
        .set mips1
s441:
        mov.s   $f4, $f6                        # 46003106
1:
        .set pop
        .set push
        .set mips1
s442:
        mov.s   $f6, $f8                        # 46004186
1:
        .set pop
        .set push
        .set mips1
s443:
        mov.d   $f2, $f4                        # 46202086
1:
        .set pop
        .set push
        .set mips1
s444:
        mov.d   $f4, $f6                        # 46203106
1:
        .set pop
        .set push
        .set mips1
s445:
        mov.d   $f6, $f8                        # 46204186
1:
        .set pop
        .set push
        .set mips1
s446:
        cvt.s.d $f2, $f4                        # 462020a0
1:
        .set pop
        .set push
        .set mips1
s447:
        cvt.s.d $f4, $f6                        # 46203120
1:
        .set pop
        .set push
        .set mips1
s448:
        cvt.s.d $f6, $f8                        # 462041a0
1:
        .set pop
        .set push
        .set mips1
s449:
        cvt.d.s $f2, $f4                        # 460020a1
1:
        .set pop
        .set push
        .set mips1
s450:
        cvt.d.s $f4, $f6                        # 46003121
1:
        .set pop
        .set push
        .set mips1
s451:
        cvt.d.s $f6, $f8                        # 460041a1
1:
        .set pop
        .set push
        .set mips1
s452:
        cvt.s.w $f2, $f4                        # 468020a0
1:
        .set pop
        .set push
        .set mips1
s453:
        cvt.s.w $f4, $f6                        # 46803120
1:
        .set pop
        .set push
        .set mips1
s454:
        cvt.s.w $f6, $f8                        # 468041a0
1:
        .set pop
        .set push
        .set mips1
s455:
        cvt.d.w $f2, $f4                        # 468020a1
1:
        .set pop
        .set push
        .set mips1
s456:
        cvt.d.w $f4, $f6                        # 46803121
1:
        .set pop
        .set push
        .set mips1
s457:
        cvt.d.w $f6, $f8                        # 468041a1
1:
        .set pop
        .set push
        .set mips3
s458:
        cvt.s.l $f2, $f4                        # 46a020a0
1:
        .set pop
        .set push
        .set mips3
s459:
        cvt.s.l $f4, $f6                        # 46a03120
1:
        .set pop
        .set push
        .set mips3
s460:
        cvt.s.l $f6, $f8                        # 46a041a0
1:
        .set pop
        .set push
        .set mips3
s461:
        cvt.d.l $f2, $f4                        # 46a020a1
1:
        .set pop
        .set push
        .set mips3
s462:
        cvt.d.l $f4, $f6                        # 46a03121
1:
        .set pop
        .set push
        .set mips3
s463:
        cvt.d.l $f6, $f8                        # 46a041a1
1:
        .set pop
        .set push
        .set mips2
s464:
        trunc.w.s $f2, $f4                      # 4600208d
1:
        .set pop
        .set push
        .set mips2
s465:
        trunc.w.s $f4, $f6                      # 4600310d
1:
        .set pop
        .set push
        .set mips2
s466:
        trunc.w.s $f6, $f8                      # 4600418d
1:
        .set pop
        .set push
        .set mips2
s467:
        trunc.w.d $f2, $f4                      # 4620208d
1:
        .set pop
        .set push
        .set mips2
s468:
        trunc.w.d $f4, $f6                      # 4620310d
1:
        .set pop
        .set push
        .set mips2
s469:
        trunc.w.d $f6, $f8                      # 4620418d
1:
        .set pop
        .set push
        .set mips3
s470:
        trunc.l.s $f2, $f4                      # 46002089
1:
        .set pop
        .set push
        .set mips3
s471:
        trunc.l.s $f4, $f6                      # 46003109
1:
        .set pop
        .set push
        .set mips3
s472:
        trunc.l.s $f6, $f8                      # 46004189
1:
        .set pop
        .set push
        .set mips3
s473:
        trunc.l.d $f2, $f4                      # 46202089
1:
        .set pop
        .set push
        .set mips3
s474:
        trunc.l.d $f4, $f6                      # 46203109
1:
        .set pop
        .set push
        .set mips3
s475:
        trunc.l.d $f6, $f8                      # 46204189
1:
        .set pop
        .set push
        .set mips1
s476:
        c.eq.s  $f2, $f4                        # 46041032
1:
        .set pop
        .set push
        .set mips1
s477:
        c.eq.s  $f4, $f6                        # 46062032
1:
        .set pop
        .set push
        .set mips1
s478:
        c.eq.s  $f6, $f8                        # 46083032
1:
        .set pop
        .set push
        .set mips4
s479:
        c.eq.s  $fcc1, $f4, $f6                 # 46062132
1:
        .set pop
        .set push
        .set mips4
s480:
        c.eq.s  $fcc2, $f6, $f8                 # 46083232
1:
        .set pop
        .set push
        .set mips4
s481:
        c.eq.s  $fcc3, $f8, $f10                # 460a4332
1:
        .set pop
        .set push
        .set mips1
s482:
        c.eq.d  $f2, $f4                        # 46241032
1:
        .set pop
        .set push
        .set mips1
s483:
        c.eq.d  $f4, $f6                        # 46262032
1:
        .set pop
        .set push
        .set mips1
s484:
        c.eq.d  $f6, $f8                        # 46283032
1:
        .set pop
        .set push
        .set mips4
s485:
        c.eq.d  $fcc1, $f4, $f6                 # 46262132
1:
        .set pop
        .set push
        .set mips4
s486:
        c.eq.d  $fcc2, $f6, $f8                 # 46283232
1:
        .set pop
        .set push
        .set mips4
s487:
        c.eq.d  $fcc3, $f8, $f10                # 462a4332
1:
        .set pop
        .set push
        .set mips1
s488:
        c.lt.s  $f2, $f4                        # 4604103c
1:
        .set pop
        .set push
        .set mips1
s489:
        c.lt.s  $f4, $f6                        # 4606203c
1:
        .set pop
        .set push
        .set mips1
s490:
        c.lt.s  $f6, $f8                        # 4608303c
1:
        .set pop
        .set push
        .set mips4
s491:
        c.lt.s  $fcc1, $f4, $f6                 # 4606213c
1:
        .set pop
        .set push
        .set mips4
s492:
        c.lt.s  $fcc2, $f6, $f8                 # 4608323c
1:
        .set pop
        .set push
        .set mips4
s493:
        c.lt.s  $fcc3, $f8, $f10                # 460a433c
1:
        .set pop
        .set push
        .set mips1
s494:
        c.lt.d  $f2, $f4                        # 4624103c
1:
        .set pop
        .set push
        .set mips1
s495:
        c.lt.d  $f4, $f6                        # 4626203c
1:
        .set pop
        .set push
        .set mips1
s496:
        c.lt.d  $f6, $f8                        # 4628303c
1:
        .set pop
        .set push
        .set mips4
s497:
        c.lt.d  $fcc1, $f4, $f6                 # 4626213c
1:
        .set pop
        .set push
        .set mips4
s498:
        c.lt.d  $fcc2, $f6, $f8                 # 4628323c
1:
        .set pop
        .set push
        .set mips4
s499:
        c.lt.d  $fcc3, $f8, $f10                # 462a433c
1:
        .set pop
        .set push
        .set mips1
s500:
        c.le.s  $f2, $f4                        # 4604103e
1:
        .set pop
        .set push
        .set mips1
s501:
        c.le.s  $f4, $f6                        # 4606203e
1:
        .set pop
        .set push
        .set mips1
s502:
        c.le.s  $f6, $f8                        # 4608303e
1:
        .set pop
        .set push
        .set mips4
s503:
        c.le.s  $fcc1, $f4, $f6                 # 4606213e
1:
        .set pop
        .set push
        .set mips4
s504:
        c.le.s  $fcc2, $f6, $f8                 # 4608323e
1:
        .set pop
        .set push
        .set mips4
s505:
        c.le.s  $fcc3, $f8, $f10                # 460a433e
1:
        .set pop
        .set push
        .set mips1
s506:
        c.le.d  $f2, $f4                        # 4624103e
1:
        .set pop
        .set push
        .set mips1
s507:
        c.le.d  $f4, $f6                        # 4626203e
1:
        .set pop
        .set push
        .set mips1
s508:
        c.le.d  $f6, $f8                        # 4628303e
1:
        .set pop
        .set push
        .set mips4
s509:
        c.le.d  $fcc1, $f4, $f6                 # 4626213e
1:
        .set pop
        .set push
        .set mips4
s510:
        c.le.d  $fcc2, $f6, $f8                 # 4628323e
1:
        .set pop
        .set push
        .set mips4
s511:
        c.le.d  $fcc3, $f8, $f10                # 462a433e
1:
        .set pop
        .set push
        .set mips1
s512:
        c.olt.s $f2, $f4                        # 46041034
1:
        .set pop
        .set push
        .set mips1
s513:
        c.olt.s $f4, $f6                        # 46062034
1:
        .set pop
        .set push
        .set mips1
s514:
        c.olt.s $f6, $f8                        # 46083034
1:
        .set pop
        .set push
        .set mips4
s515:
        c.olt.s $fcc1, $f4, $f6                 # 46062134
1:
        .set pop
        .set push
        .set mips4
s516:
        c.olt.s $fcc2, $f6, $f8                 # 46083234
1:
        .set pop
        .set push
        .set mips4
s517:
        c.olt.s $fcc3, $f8, $f10                # 460a4334
1:
        .set pop
        .set push
        .set mips1
s518:
        c.olt.d $f2, $f4                        # 46241034
1:
        .set pop
        .set push
        .set mips1
s519:
        c.olt.d $f4, $f6                        # 46262034
1:
        .set pop
        .set push
        .set mips1
s520:
        c.olt.d $f6, $f8                        # 46283034
1:
        .set pop
        .set push
        .set mips4
s521:
        c.olt.d $fcc1, $f4, $f6                 # 46262134
1:
        .set pop
        .set push
        .set mips4
s522:
        c.olt.d $fcc2, $f6, $f8                 # 46283234
1:
        .set pop
        .set push
        .set mips4
s523:
        c.olt.d $fcc3, $f8, $f10                # 462a4334
1:
        .set pop
        .set push
        .set mips1
s524:
        c.un.s  $f2, $f4                        # 46041031
1:
        .set pop
        .set push
        .set mips1
s525:
        c.un.s  $f4, $f6                        # 46062031
1:
        .set pop
        .set push
        .set mips1
s526:
        c.un.s  $f6, $f8                        # 46083031
1:
        .set pop
        .set push
        .set mips4
s527:
        c.un.s  $fcc1, $f4, $f6                 # 46062131
1:
        .set pop
        .set push
        .set mips4
s528:
        c.un.s  $fcc2, $f6, $f8                 # 46083231
1:
        .set pop
        .set push
        .set mips4
s529:
        c.un.s  $fcc3, $f8, $f10                # 460a4331
1:
        .set pop
        .set push
        .set mips1
s530:
        c.un.d  $f2, $f4                        # 46241031
1:
        .set pop
        .set push
        .set mips1
s531:
        c.un.d  $f4, $f6                        # 46262031
1:
        .set pop
        .set push
        .set mips1
s532:
        c.un.d  $f6, $f8                        # 46283031
1:
        .set pop
        .set push
        .set mips4
s533:
        c.un.d  $fcc1, $f4, $f6                 # 46262131
1:
        .set pop
        .set push
        .set mips4
s534:
        c.un.d  $fcc2, $f6, $f8                 # 46283231
1:
        .set pop
        .set push
        .set mips4
s535:
        c.un.d  $fcc3, $f8, $f10                # 462a4331
1:
        .set pop
        .set push
        .set mips1
s536:
        mfc1    $2, $f4                         # 44022000
1:
        .set pop
        .set push
        .set mips1
s537:
        mfc1    $3, $f6                         # 44033000
1:
        .set pop
        .set push
        .set mips1
s538:
        mfc1    $4, $f8                         # 44044000
1:
        .set pop
        .set push
        .set mips1
s539:
        mtc1    $2, $f4                         # 44822000
1:
        .set pop
        .set push
        .set mips1
s540:
        mtc1    $3, $f6                         # 44833000
1:
        .set pop
        .set push
        .set mips1
s541:
        mtc1    $4, $f8                         # 44844000
1:
        .set pop
        .set push
        .set mips3
s542:
        dmfc1   $2, $f4                         # 44222000
1:
        .set pop
        .set push
        .set mips3
s543:
        dmfc1   $3, $f6                         # 44233000
1:
        .set pop
        .set push
        .set mips3
s544:
        dmfc1   $4, $f8                         # 44244000
1:
        .set pop
        .set push
        .set mips3
s545:
        dmtc1   $2, $f4                         # 44a22000
1:
        .set pop
        .set push
        .set mips3
s546:
        dmtc1   $3, $f6                         # 44a33000
1:
        .set pop
        .set push
        .set mips3
s547:
        dmtc1   $4, $f8                         # 44a44000
1:
        .set pop
        .set push
        .set mips32r2
s548:
        mfhc1   $2, $f4                         # 44622000
1:
        .set pop
        .set push
        .set mips32r2
s549:
        mfhc1   $3, $f6                         # 44633000
1:
        .set pop
        .set push
        .set mips32r2
s550:
        mfhc1   $4, $f8                         # 44644000
1:
        .set pop
        .set push
        .set mips32r2
s551:
        mthc1   $2, $f4                         # 44e22000
1:
        .set pop
        .set push
        .set mips32r2
s552:
        mthc1   $3, $f6                         # 44e33000
1:
        .set pop
        .set push
        .set mips32r2
s553:
        mthc1   $4, $f8                         # 44e44000
1:
        .set pop
        .set push
        .set mips1
s554:
        cfc1    $2, $31                         # 4442f800
1:
        .set pop
        .set push
        .set mips1
s555:
        cfc1    $3, $f25                        # 4443c800
1:
        .set pop
        .set push
        .set mips1
s556:
        cfc1    $4, $f25                        # 4444c800
1:
        .set pop
        .set push
        .set mips1
s557:
        ctc1    $2, $31                         # 44c2f800
1:
        .set pop
        .set push
        .set mips1
s558:
        ctc1    $3, $f25                        # 44c3c800
1:
        .set pop
        .set push
        .set mips1
s559:
        ctc1    $4, $f25                        # 44c4c800
1:
        .set pop
        .set push
        .set mips1
s560:
        nop                                     # 00000000
1:
        .set pop
        .set push
        .set mips2
s561:
        sync                                    # 0000000f
1:
        .set pop
        .set push
        .set mips32
s562:
        sync    5                               # 0000014f
1:
        .set pop
        .set push
        .set mips32
s563:
        sync    1                               # 0000004f
1:
        .set pop
        .set push
        .set mips32
s564:
        sync    31                              # 000007cf
1:
        .set pop
        .set push
        .set mips1
s565:
        break   5, 5                            # 0005014d
1:
        .set pop
        .set push
        .set mips1
s566:
        break   1, 1                            # 0001004d
1:
        .set pop
        .set push
        .set mips1
s567:
        break   1023, 1023                      # 03ffffcd
1:
        .set pop
        .set push
        .set mips1
s568:
        syscall 5                               # 0000014c
1:
        .set pop
        .set push
        .set mips1
s569:
        syscall 1                               # 0000004c
1:
        .set pop
        .set push
        .set mips1
s570:
        syscall 1048575                         # 03ffffcc
1:
        .set pop
        .set push
        .set mips1
s571:
        ehb                                     # 000000c0
1:
        .set pop
        .set push
        .set mips3
s572:
        eret                                    # 42000018
1:
        .set pop
        .set push
        .set mips3
s573:
        cache   5, 8($4)                        # bc850008
1:
        .set pop
        .set push
        .set mips3
s574:
        cache   1, ($5)                         # bca10000
1:
        .set pop
        .set push
        .set mips3
s575:
        cache   31, %lo(external)($6)           # bcdf0000
1:
        .set pop
        .set push
        .set mips1
s576:
        mfc0    $2, $12                         # 40026000
1:
        .set pop
        .set push
        .set mips1
s577:
        mfc0    $3, $9                          # 40034800
1:
        .set pop
        .set push
        .set mips1
s578:
        mfc0    $4, $9                          # 40044800
1:
        .set pop
        .set push
        .set mips32
s579:
        mfc0    $2, $12, 5                      # 40026005
1:
        .set pop
        .set push
        .set mips32
s580:
        mfc0    $3, $9, 1                       # 40034801
1:
        .set pop
        .set push
        .set mips32
s581:
        mfc0    $4, $9, 7                       # 40044807
1:
        .set pop
        .set push
        .set mips1
s582:
        mtc0    $2, $12                         # 40826000
1:
        .set pop
        .set push
        .set mips1
s583:
        mtc0    $3, $9                          # 40834800
1:
        .set pop
        .set push
        .set mips1
s584:
        mtc0    $4, $9                          # 40844800
1:
        .set pop
        .set push
        .set mips32
s585:
        mtc0    $2, $12, 5                      # 40826005
1:
        .set pop
        .set push
        .set mips32
s586:
        mtc0    $3, $9, 1                       # 40834801
1:
        .set pop
        .set push
        .set mips32
s587:
        mtc0    $4, $9, 7                       # 40844807
1:
        .set pop
        .set push
        .set mips3
s588:
        dmfc0   $2, $12                         # 40226000
1:
        .set pop
        .set push
        .set mips3
s589:
        dmfc0   $3, $9                          # 40234800
1:
        .set pop
        .set push
        .set mips3
s590:
        dmfc0   $4, $9                          # 40244800
1:
        .set pop
        .set push
        .set mips64
s591:
        dmfc0   $2, $12, 5                      # 40226005
1:
        .set pop
        .set push
        .set mips64
s592:
        dmfc0   $3, $9, 1                       # 40234801
1:
        .set pop
        .set push
        .set mips64
s593:
        dmfc0   $4, $9, 7                       # 40244807
1:
        .set pop
        .set push
        .set mips3
s594:
        dmtc0   $2, $12                         # 40a26000
1:
        .set pop
        .set push
        .set mips3
s595:
        dmtc0   $3, $9                          # 40a34800
1:
        .set pop
        .set push
        .set mips3
s596:
        dmtc0   $4, $9                          # 40a44800
1:
        .set pop
        .set push
        .set mips64
s597:
        dmtc0   $2, $12, 5                      # 40a26005
1:
        .set pop
        .set push
        .set mips64
s598:
        dmtc0   $3, $9, 1                       # 40a34801
1:
        .set pop
        .set push
        .set mips64
s599:
        dmtc0   $4, $9, 7                       # 40a44807
1:
        .set pop
        .set push
        .set mips1
s600:
        tlbp                                    # 42000008
1:
        .set pop
        .set push
        .set mips1
s601:
        tlbr                                    # 42000001
1:
        .set pop
        .set push
        .set mips1
s602:
        tlbwi                                   # 42000002
1:
        .set pop
        .set push
        .set mips1
s603:
        tlbwr                                   # 42000006
1:
        .set pop
        .set push
        .set mips1
s604:
        rfe                                     # 42000010
1:
        .set pop
        .set push
        .set mips4
s605:
        movn.s  $f2, $f4, $4                    # 46042093
1:
        .set pop
        .set push
        .set mips4
s606:
        movn.s  $f4, $f6, $5                    # 46053113
1:
        .set pop
        .set push
        .set mips4
s607:
        movn.s  $f6, $f8, $6                    # 46064193
1:
        .set pop
        .set push
        .set mips4
s608:
        movn.d  $f2, $f4, $4                    # 46242093
1:
        .set pop
        .set push
        .set mips4
s609:
        movn.d  $f4, $f6, $5                    # 46253113
1:
        .set pop
        .set push
        .set mips4
s610:
        movn.d  $f6, $f8, $6                    # 46264193
1:
        .set pop
        .set push
        .set mips4
s611:
        movz.s  $f2, $f4, $4                    # 46042092
1:
        .set pop
        .set push
        .set mips4
s612:
        movz.s  $f4, $f6, $5                    # 46053112
1:
        .set pop
        .set push
        .set mips4
s613:
        movz.s  $f6, $f8, $6                    # 46064192
1:
        .set pop
        .set push
        .set mips4
s614:
        movz.d  $f2, $f4, $4                    # 46242092
1:
        .set pop
        .set push
        .set mips4
s615:
        movz.d  $f4, $f6, $5                    # 46253112
1:
        .set pop
        .set push
        .set mips4
s616:
        movz.d  $f6, $f8, $6                    # 46264192
1:
        .set pop
        .set push
        .set mips4
s617:
        movt.s  $f2, $f4, $fcc1                 # 46052091
1:
        .set pop
        .set push
        .set mips4
s618:
        movt.s  $f4, $f6, $fcc2                 # 46093111
1:
        .set pop
        .set push
        .set mips4
s619:
        movt.s  $f6, $f8, $fcc3                 # 460d4191
1:
        .set pop
        .set push
        .set mips4
s620:
        movt.d  $f2, $f4, $fcc1                 # 46252091
1:
        .set pop
        .set push
        .set mips4
s621:
        movt.d  $f4, $f6, $fcc2                 # 46293111
1:
        .set pop
        .set push
        .set mips4
s622:
        movt.d  $f6, $f8, $fcc3                 # 462d4191
1:
        .set pop
        .set push
        .set mips4
s623:
        movf.s  $f2, $f4, $fcc1                 # 46042091
1:
        .set pop
        .set push
        .set mips4
s624:
        movf.s  $f4, $f6, $fcc2                 # 46083111
1:
        .set pop
        .set push
        .set mips4
s625:
        movf.s  $f6, $f8, $fcc3                 # 460c4191
1:
        .set pop
        .set push
        .set mips4
s626:
        movf.d  $f2, $f4, $fcc1                 # 46242091
1:
        .set pop
        .set push
        .set mips4
s627:
        movf.d  $f4, $f6, $fcc2                 # 46283111
1:
        .set pop
        .set push
        .set mips4
s628:
        movf.d  $f6, $f8, $fcc3                 # 462c4191
1:
        .set pop
        .set push
        .set mips4
s629:
        movz    $2, $3, $4                      # 0064100a
1:
        .set pop
        .set push
        .set mips4
s630:
        movz    $3, $4, $5                      # 0085180a
1:
        .set pop
        .set push
        .set mips4
s631:
        movz    $4, $5, $6                      # 00a6200a
1:
        .set pop
        .set push
        .set mips4
s632:
        movf    $2, $3, $fcc1                   # 00641001
1:
        .set pop
        .set push
        .set mips4
s633:
        movf    $3, $4, $fcc2                   # 00881801
1:
        .set pop
        .set push
        .set mips4
s634:
        movf    $4, $5, $fcc3                   # 00ac2001
1:
        .set pop
        .set push
        .set mips4
s635:
        movt    $2, $3, $fcc1                   # 00651001
1:
        .set pop
        .set push
        .set mips4
s636:
        movt    $3, $4, $fcc2                   # 00891801
1:
        .set pop
        .set push
        .set mips4
s637:
        movt    $4, $5, $fcc3                   # 00ad2001
1:
        .set pop
        .set push
        .set mips1
s638:
        add     $2, $3, $4                      # 00641020
1:
        .set pop
        .set push
        .set mips1
s639:
        add     $3, $4, $5                      # 00851820
1:
        .set pop
        .set push
        .set mips1
s640:
        add     $4, $5, $6                      # 00a62020
1:
        .set pop
        .set push
        .set mips1
s641:
        add     $2, $3, 12                      # 2062000c
1:
        .set pop
        .set push
        .set mips1
s642:
        add     $3, $4, 0x12345                 # 3c010001 34212345 00811820
1:
        .set pop
        .set push
        .set mips1
s643:
        add     $4, $5, 12                      # 20a4000c
1:
        .set pop
        .set push
        .set mips1
s644:
        addi    $2, $3, -5                      # 2062fffb
1:
        .set pop
        .set push
        .set mips1
s645:
        addi    $3, $4, 3                       # 20830003
1:
        .set pop
        .set push
        .set mips1
s646:
        addi    $4, $5, %lo(external)           # 20a40000
1:
        .set pop
        .set push
        .set mips1
s647:
        sub     $2, $3, $4                      # 00641022
1:
        .set pop
        .set push
        .set mips1
s648:
        sub     $3, $4, $5                      # 00851822
1:
        .set pop
        .set push
        .set mips1
s649:
        sub     $4, $5, $6                      # 00a62022
1:
        .set pop
        .set push
        .set mips1
s650:
        sub     $2, $3, 12                      # 2062fff4
1:
        .set pop
        .set push
        .set mips1
s651:
        sub     $3, $4, 0x12345                 # 3c010001 34212345 00811822
1:
        .set pop
        .set push
        .set mips1
s652:
        sub     $4, $5, 12                      # 20a4fff4
1:
        .set pop
        .set push
        .set mips3
s653:
        dadd    $2, $3, $4                      # 0064102c
1:
        .set pop
        .set push
        .set mips3
s654:
        dadd    $3, $4, $5                      # 0085182c
1:
        .set pop
        .set push
        .set mips3
s655:
        dadd    $4, $5, $6                      # 00a6202c
1:
        .set pop
        .set push
        .set mips3
s656:
        dadd    $2, $3, 12                      # 6062000c
1:
        .set pop
        .set push
        .set mips3
s657:
        dadd    $3, $4, 0x12345                 # 3c010001 34212345 0081182c
1:
        .set pop
        .set push
        .set mips3
s658:
        dadd    $4, $5, 12                      # 60a4000c
1:
        .set pop
        .set push
        .set mips3
s659:
        daddi   $2, $3, -5                      # 6062fffb
1:
        .set pop
        .set push
        .set mips3
s660:
        daddi   $3, $4, 3                       # 60830003
1:
        .set pop
        .set push
        .set mips3
s661:
        daddi   $4, $5, %lo(external)           # 60a40000
1:
        .set pop
        .set push
        .set mips3
s662:
        dsub    $2, $3, $4                      # 0064102e
1:
        .set pop
        .set push
        .set mips3
s663:
        dsub    $3, $4, $5                      # 0085182e
1:
        .set pop
        .set push
        .set mips3
s664:
        dsub    $4, $5, $6                      # 00a6202e
1:
        .set pop
        .set push
        .set mips3
s665:
        dsub    $2, $3, 12                      # 6062fff4
1:
        .set pop
        .set push
        .set mips3
s666:
        dsub    $3, $4, 0x12345                 # 3c010001 34212345 0081182e
1:
        .set pop
        .set push
        .set mips3
s667:
        dsub    $4, $5, 12                      # 60a4fff4
1:
        .set pop
        .set push
        .set mips1
s668:
        negu    $2, $3                          # 00031023
1:
        .set pop
        .set push
        .set mips1
s669:
        negu    $3, $4                          # 00041823
1:
        .set pop
        .set push
        .set mips1
s670:
        negu    $4, $5                          # 00052023
1:
        .set pop
        .set push
        .set mips1
s671:
        neg     $2, $3                          # 00031022
1:
        .set pop
        .set push
        .set mips1
s672:
        neg     $3, $4                          # 00041822
1:
        .set pop
        .set push
        .set mips1
s673:
        neg     $4, $5                          # 00052022
1:
        .set pop
        .set push
        .set mips1
s674:
        not     $2, $3                          # 00601027
1:
        .set pop
        .set push
        .set mips1
s675:
        not     $3, $4                          # 00801827
1:
        .set pop
        .set push
        .set mips1
s676:
        not     $4, $5                          # 00a02027
1:
        .set pop
        .set push
        .set mips3
s677:
        dli     $2, 0x123456789abcdef0          # 3c021234 34425678 00021438 34429abc 00021438 3442def0
1:
        .set pop
        .set push
        .set mips3
s678:
        dli     $3, -5                          # 2403fffb
1:
        .set pop
        .set push
        .set mips3
s679:
        dli     $4, 0x100000000                 # 34048000 00042478
1:
        .set pop
        .set push
        .set mips1
s680:
        ssnop                                   # 00000040
1:
        .set pop
        .set push
        .set mips32
s681:
        wait                                    # 42000020
1:
        .set pop
        .set push
        .set mips3
s682:
        wait                                    # 42000020
1:
        .set pop
        .set push
        .set mips2
s683:
        teq     $2, $3, 5                       # 00430174
1:
        .set pop
        .set push
        .set mips2
s684:
        teq     $3, $4, 1                       # 00640074
1:
        .set pop
        .set push
        .set mips2
s685:
        teq     $4, $5, 1023                    # 0085fff4
1:
        .set pop
        .set push
        .set mips2
s686:
        teq     $2, 12                          # 044c000c
1:
        .set pop
        .set push
        .set mips2
s687:
        teq     $3, 0x12345                     # 3c010001 34212345 00610034
1:
        .set pop
        .set push
        .set mips2
s688:
        teq     $4, 12                          # 048c000c
1:
        .set pop
        .set push
        .set mips2
s689:
        tne     $2, $3, 5                       # 00430176
1:
        .set pop
        .set push
        .set mips2
s690:
        tne     $3, $4, 1                       # 00640076
1:
        .set pop
        .set push
        .set mips2
s691:
        tne     $4, $5, 1023                    # 0085fff6
1:
        .set pop
        .set push
        .set mips2
s692:
        tne     $2, 12                          # 044e000c
1:
        .set pop
        .set push
        .set mips2
s693:
        tne     $3, 0x12345                     # 3c010001 34212345 00610036
1:
        .set pop
        .set push
        .set mips2
s694:
        tne     $4, 12                          # 048e000c
1:
        .set pop
        .set push
        .set mips2
s695:
        tge     $2, $3, 5                       # 00430170
1:
        .set pop
        .set push
        .set mips2
s696:
        tge     $3, $4, 1                       # 00640070
1:
        .set pop
        .set push
        .set mips2
s697:
        tge     $4, $5, 1023                    # 0085fff0
1:
        .set pop
        .set push
        .set mips2
s698:
        tge     $2, 12                          # 0448000c
1:
        .set pop
        .set push
        .set mips2
s699:
        tge     $3, 0x12345                     # 3c010001 34212345 00610030
1:
        .set pop
        .set push
        .set mips2
s700:
        tge     $4, 12                          # 0488000c
1:
        .set pop
        .set push
        .set mips2
s701:
        tgeu    $2, $3, 5                       # 00430171
1:
        .set pop
        .set push
        .set mips2
s702:
        tgeu    $3, $4, 1                       # 00640071
1:
        .set pop
        .set push
        .set mips2
s703:
        tgeu    $4, $5, 1023                    # 0085fff1
1:
        .set pop
        .set push
        .set mips2
s704:
        tgeu    $2, 12                          # 0449000c
1:
        .set pop
        .set push
        .set mips2
s705:
        tgeu    $3, 0x12345                     # 3c010001 34212345 00610031
1:
        .set pop
        .set push
        .set mips2
s706:
        tgeu    $4, 12                          # 0489000c
1:
        .set pop
        .set push
        .set mips2
s707:
        tlt     $2, $3, 5                       # 00430172
1:
        .set pop
        .set push
        .set mips2
s708:
        tlt     $3, $4, 1                       # 00640072
1:
        .set pop
        .set push
        .set mips2
s709:
        tlt     $4, $5, 1023                    # 0085fff2
1:
        .set pop
        .set push
        .set mips2
s710:
        tlt     $2, 12                          # 044a000c
1:
        .set pop
        .set push
        .set mips2
s711:
        tlt     $3, 0x12345                     # 3c010001 34212345 00610032
1:
        .set pop
        .set push
        .set mips2
s712:
        tlt     $4, 12                          # 048a000c
1:
        .set pop
        .set push
        .set mips2
s713:
        tltu    $2, $3, 5                       # 00430173
1:
        .set pop
        .set push
        .set mips2
s714:
        tltu    $3, $4, 1                       # 00640073
1:
        .set pop
        .set push
        .set mips2
s715:
        tltu    $4, $5, 1023                    # 0085fff3
1:
        .set pop
        .set push
        .set mips2
s716:
        tltu    $2, 12                          # 044b000c
1:
        .set pop
        .set push
        .set mips2
s717:
        tltu    $3, 0x12345                     # 3c010001 34212345 00610033
1:
        .set pop
        .set push
        .set mips2
s718:
        tltu    $4, 12                          # 048b000c
1:
        .set pop
        .set push
        .set mips2
s719:
        teqi    $2, -5                          # 044cfffb
1:
        .set pop
        .set push
        .set mips2
s720:
        teqi    $3, 3                           # 046c0003
1:
        .set pop
        .set push
        .set mips2
s721:
        teqi    $4, 32767                       # 048c7fff
1:
        .set pop
        .set push
        .set mips2
s722:
        tnei    $2, -5                          # 044efffb
1:
        .set pop
        .set push
        .set mips2
s723:
        tnei    $3, 3                           # 046e0003
1:
        .set pop
        .set push
        .set mips2
s724:
        tnei    $4, 32767                       # 048e7fff
1:
        .set pop
        .set push
        .set mips2
s725:
        tgei    $2, -5                          # 0448fffb
1:
        .set pop
        .set push
        .set mips2
s726:
        tgei    $3, 3                           # 04680003
1:
        .set pop
        .set push
        .set mips2
s727:
        tgei    $4, 32767                       # 04887fff
1:
        .set pop
        .set push
        .set mips2
s728:
        tgeiu   $2, -5                          # 0449fffb
1:
        .set pop
        .set push
        .set mips2
s729:
        tgeiu   $3, 3                           # 04690003
1:
        .set pop
        .set push
        .set mips2
s730:
        tgeiu   $4, 32767                       # 04897fff
1:
        .set pop
        .set push
        .set mips2
s731:
        tlti    $2, -5                          # 044afffb
1:
        .set pop
        .set push
        .set mips2
s732:
        tlti    $3, 3                           # 046a0003
1:
        .set pop
        .set push
        .set mips2
s733:
        tlti    $4, 32767                       # 048a7fff
1:
        .set pop
        .set push
        .set mips2
s734:
        tltiu   $2, -5                          # 044bfffb
1:
        .set pop
        .set push
        .set mips2
s735:
        tltiu   $3, 3                           # 046b0003
1:
        .set pop
        .set push
        .set mips2
s736:
        tltiu   $4, 32767                       # 048b7fff
1:
        .set pop
        .set push
        .set mips32
s737:
        mul     $2, $3, $4                      # 70641002
1:
        .set pop
        .set push
        .set mips32
s738:
        mul     $3, $4, $5                      # 70851802
1:
        .set pop
        .set push
        .set mips32
s739:
        mul     $4, $5, $6                      # 70a62002
1:
        .set pop
        .set push
        .set mips1
s740:
        mul     $2, $3, 12                      # 2401000c 00610018 00001012
1:
        .set pop
        .set push
        .set mips1
s741:
        mul     $3, $4, 0x12345                 # 3c010001 34212345 00810018 00001812
1:
        .set pop
        .set push
        .set mips1
s742:
        mul     $4, $5, 12                      # 2401000c 00a10018 00002012
1:
        .set pop
        .set push
        .set mips1
s743:
        mul     $2, $3, $4                      # 00640019 00001012
1:
        .set pop
        .set push
        .set mips1
s744:
        mul     $3, $4, $5                      # 00850019 00001812
1:
        .set pop
        .set push
        .set mips1
s745:
        mul     $4, $5, $6                      # 00a60019 00002012
1:
        .set pop
        .set push
        .set mips32
s746:
        madd    $2, $3                          # 70430000
1:
        .set pop
        .set push
        .set mips32
s747:
        madd    $3, $4                          # 70640000
1:
        .set pop
        .set push
        .set mips32
s748:
        madd    $4, $5                          # 70850000
1:
        .set pop
        .set push
        .set arch=allegrex
s749:
        madd    $2, $3                          # 0043001c
1:
        .set pop
        .set push
        .set arch=allegrex
s750:
        madd    $3, $4                          # 0064001c
1:
        .set pop
        .set push
        .set arch=allegrex
s751:
        madd    $4, $5                          # 0085001c
1:
        .set pop
        .set push
        .set mips32
s752:
        maddu   $2, $3                          # 70430001
1:
        .set pop
        .set push
        .set mips32
s753:
        maddu   $3, $4                          # 70640001
1:
        .set pop
        .set push
        .set mips32
s754:
        maddu   $4, $5                          # 70850001
1:
        .set pop
        .set push
        .set arch=allegrex
s755:
        maddu   $2, $3                          # 0043001d
1:
        .set pop
        .set push
        .set arch=allegrex
s756:
        maddu   $3, $4                          # 0064001d
1:
        .set pop
        .set push
        .set arch=allegrex
s757:
        maddu   $4, $5                          # 0085001d
1:
        .set pop
        .set push
        .set mips32
s758:
        msub    $2, $3                          # 70430004
1:
        .set pop
        .set push
        .set mips32
s759:
        msub    $3, $4                          # 70640004
1:
        .set pop
        .set push
        .set mips32
s760:
        msub    $4, $5                          # 70850004
1:
        .set pop
        .set push
        .set arch=allegrex
s761:
        msub    $2, $3                          # 0043002e
1:
        .set pop
        .set push
        .set arch=allegrex
s762:
        msub    $3, $4                          # 0064002e
1:
        .set pop
        .set push
        .set arch=allegrex
s763:
        msub    $4, $5                          # 0085002e
1:
        .set pop
        .set push
        .set mips32
s764:
        msubu   $2, $3                          # 70430005
1:
        .set pop
        .set push
        .set mips32
s765:
        msubu   $3, $4                          # 70640005
1:
        .set pop
        .set push
        .set mips32
s766:
        msubu   $4, $5                          # 70850005
1:
        .set pop
        .set push
        .set arch=allegrex
s767:
        msubu   $2, $3                          # 0043002f
1:
        .set pop
        .set push
        .set arch=allegrex
s768:
        msubu   $3, $4                          # 0064002f
1:
        .set pop
        .set push
        .set arch=allegrex
s769:
        msubu   $4, $5                          # 0085002f
1:
        .set pop
        .set push
        .set mips32r2
s770:
        ext     $2, $3, 4, 8                    # 7c623900
1:
        .set pop
        .set push
        .set mips32r2
s771:
        ext     $3, $4, 0, 32                   # 7c83f800
1:
        .set pop
        .set push
        .set mips32r2
s772:
        ext     $4, $5, 31, 1                   # 7ca407c0
1:
        .set pop
        .set push
        .set mips32r2
s773:
        ins     $2, $3, 4, 8                    # 7c625904
1:
        .set pop
        .set push
        .set mips32r2
s774:
        ins     $3, $4, 0, 32                   # 7c83f804
1:
        .set pop
        .set push
        .set mips32r2
s775:
        ins     $4, $5, 31, 1                   # 7ca4ffc4
1:
        .set pop
        .set push
        .set mips32r2
s776:
        seb     $2, $3                          # 7c031420
1:
        .set pop
        .set push
        .set mips32r2
s777:
        seb     $3, $4                          # 7c041c20
1:
        .set pop
        .set push
        .set mips32r2
s778:
        seb     $4, $5                          # 7c052420
1:
        .set pop
        .set push
        .set mips32r2
s779:
        seh     $2, $3                          # 7c031620
1:
        .set pop
        .set push
        .set mips32r2
s780:
        seh     $3, $4                          # 7c041e20
1:
        .set pop
        .set push
        .set mips32r2
s781:
        seh     $4, $5                          # 7c052620
1:
        .set pop
        .set push
        .set mips4
s782:
        pref    5, 8($4)                        # cc850008
1:
        .set pop
        .set push
        .set mips4
s783:
        pref    1, ($5)                         # cca10000
1:
        .set pop
        .set push
        .set mips4
s784:
        pref    31, %lo(external)($6)           # ccdf0000
1:
        .set pop
        .set push
        .set arch=allegrex
s785:
        max     $2, $3, $4                      # 0064102c
1:
        .set pop
        .set push
        .set arch=allegrex
s786:
        max     $3, $4, $5                      # 0085182c
1:
        .set pop
        .set push
        .set arch=allegrex
s787:
        max     $4, $5, $6                      # 00a6202c
1:
        .set pop
        .set push
        .set arch=allegrex
s788:
        min     $2, $3, $4                      # 0064102d
1:
        .set pop
        .set push
        .set arch=allegrex
s789:
        min     $3, $4, $5                      # 0085182d
1:
        .set pop
        .set push
        .set arch=allegrex
s790:
        min     $4, $5, $6                      # 00a6202d
1:
        .set pop
        .set push
        .set arch=allegrex
s791:
        bitrev  $2, $3                          # 7c031520
1:
        .set pop
        .set push
        .set arch=allegrex
s792:
        bitrev  $3, $4                          # 7c041d20
1:
        .set pop
        .set push
        .set arch=allegrex
s793:
        bitrev  $4, $5                          # 7c052520
1:
        .set pop
        .set push
        .set arch=allegrex
s794:
        mfic    $2, $12                         # 70026024
1:
        .set pop
        .set push
        .set arch=allegrex
s795:
        mfic    $3, $9                          # 70034824
1:
        .set pop
        .set push
        .set arch=allegrex
s796:
        mfic    $4, $9                          # 70044824
1:
        .set pop
        .set push
        .set arch=allegrex
s797:
        mtic    $2, $12                         # 70026026
1:
        .set pop
        .set push
        .set arch=allegrex
s798:
        mtic    $3, $9                          # 70034826
1:
        .set pop
        .set push
        .set arch=allegrex
s799:
        mtic    $4, $9                          # 70044826
1:
        .set pop
        .set push
        .set mips1
s800:
        cvt.w.s $f2, $f4                        # 460020a4
1:
        .set pop
        .set push
        .set mips1
s801:
        cvt.w.s $f4, $f6                        # 46003124
1:
        .set pop
        .set push
        .set mips1
s802:
        cvt.w.s $f6, $f8                        # 460041a4
1:
        .set pop
        .set push
        .set mips1
s803:
        cvt.w.d $f2, $f4                        # 462020a4
1:
        .set pop
        .set push
        .set mips1
s804:
        cvt.w.d $f4, $f6                        # 46203124
1:
        .set pop
        .set push
        .set mips1
s805:
        cvt.w.d $f6, $f8                        # 462041a4
1:
        .set pop
        .set push
        .set mips3
s806:
        cvt.l.s $f2, $f4                        # 460020a5
1:
        .set pop
        .set push
        .set mips3
s807:
        cvt.l.s $f4, $f6                        # 46003125
1:
        .set pop
        .set push
        .set mips3
s808:
        cvt.l.s $f6, $f8                        # 460041a5
1:
        .set pop
        .set push
        .set mips3
s809:
        cvt.l.d $f2, $f4                        # 462020a5
1:
        .set pop
        .set push
        .set mips3
s810:
        cvt.l.d $f4, $f6                        # 46203125
1:
        .set pop
        .set push
        .set mips3
s811:
        cvt.l.d $f6, $f8                        # 462041a5
1:
        .set pop
        .set push
        .set mips2
s812:
        round.w.s $f2, $f4                      # 4600208c
1:
        .set pop
        .set push
        .set mips2
s813:
        round.w.s $f4, $f6                      # 4600310c
1:
        .set pop
        .set push
        .set mips2
s814:
        round.w.s $f6, $f8                      # 4600418c
1:
        .set pop
        .set push
        .set mips2
s815:
        round.w.d $f2, $f4                      # 4620208c
1:
        .set pop
        .set push
        .set mips2
s816:
        round.w.d $f4, $f6                      # 4620310c
1:
        .set pop
        .set push
        .set mips2
s817:
        round.w.d $f6, $f8                      # 4620418c
1:
        .set pop
        .set push
        .set mips2
s818:
        ceil.w.s $f2, $f4                       # 4600208e
1:
        .set pop
        .set push
        .set mips2
s819:
        ceil.w.s $f4, $f6                       # 4600310e
1:
        .set pop
        .set push
        .set mips2
s820:
        ceil.w.s $f6, $f8                       # 4600418e
1:
        .set pop
        .set push
        .set mips2
s821:
        ceil.w.d $f2, $f4                       # 4620208e
1:
        .set pop
        .set push
        .set mips2
s822:
        ceil.w.d $f4, $f6                       # 4620310e
1:
        .set pop
        .set push
        .set mips2
s823:
        ceil.w.d $f6, $f8                       # 4620418e
1:
        .set pop
        .set push
        .set mips2
s824:
        floor.w.s $f2, $f4                      # 4600208f
1:
        .set pop
        .set push
        .set mips2
s825:
        floor.w.s $f4, $f6                      # 4600310f
1:
        .set pop
        .set push
        .set mips2
s826:
        floor.w.s $f6, $f8                      # 4600418f
1:
        .set pop
        .set push
        .set mips2
s827:
        floor.w.d $f2, $f4                      # 4620208f
1:
        .set pop
        .set push
        .set mips2
s828:
        floor.w.d $f4, $f6                      # 4620310f
1:
        .set pop
        .set push
        .set mips2
s829:
        floor.w.d $f6, $f8                      # 4620418f
1:
        .set pop
        .set push
        .set mips3
s830:
        round.l.s $f2, $f4                      # 46002088
1:
        .set pop
        .set push
        .set mips3
s831:
        round.l.s $f4, $f6                      # 46003108
1:
        .set pop
        .set push
        .set mips3
s832:
        round.l.s $f6, $f8                      # 46004188
1:
        .set pop
        .set push
        .set mips3
s833:
        round.l.d $f2, $f4                      # 46202088
1:
        .set pop
        .set push
        .set mips3
s834:
        round.l.d $f4, $f6                      # 46203108
1:
        .set pop
        .set push
        .set mips3
s835:
        round.l.d $f6, $f8                      # 46204188
1:
        .set pop
        .set push
        .set mips3
s836:
        ceil.l.s $f2, $f4                       # 4600208a
1:
        .set pop
        .set push
        .set mips3
s837:
        ceil.l.s $f4, $f6                       # 4600310a
1:
        .set pop
        .set push
        .set mips3
s838:
        ceil.l.s $f6, $f8                       # 4600418a
1:
        .set pop
        .set push
        .set mips3
s839:
        ceil.l.d $f2, $f4                       # 4620208a
1:
        .set pop
        .set push
        .set mips3
s840:
        ceil.l.d $f4, $f6                       # 4620310a
1:
        .set pop
        .set push
        .set mips3
s841:
        ceil.l.d $f6, $f8                       # 4620418a
1:
        .set pop
        .set push
        .set mips3
s842:
        floor.l.s $f2, $f4                      # 4600208b
1:
        .set pop
        .set push
        .set mips3
s843:
        floor.l.s $f4, $f6                      # 4600310b
1:
        .set pop
        .set push
        .set mips3
s844:
        floor.l.s $f6, $f8                      # 4600418b
1:
        .set pop
        .set push
        .set mips3
s845:
        floor.l.d $f2, $f4                      # 4620208b
1:
        .set pop
        .set push
        .set mips3
s846:
        floor.l.d $f4, $f6                      # 4620310b
1:
        .set pop
        .set push
        .set mips3
s847:
        floor.l.d $f6, $f8                      # 4620418b
1:
        .set pop
        .set push
        .set mips1
s848:
        c.f.s   $f2, $f4                        # 46041030
1:
        .set pop
        .set push
        .set mips1
s849:
        c.f.s   $f4, $f6                        # 46062030
1:
        .set pop
        .set push
        .set mips1
s850:
        c.f.s   $f6, $f8                        # 46083030
1:
        .set pop
        .set push
        .set mips4
s851:
        c.f.s   $fcc1, $f4, $f6                 # 46062130
1:
        .set pop
        .set push
        .set mips4
s852:
        c.f.s   $fcc2, $f6, $f8                 # 46083230
1:
        .set pop
        .set push
        .set mips4
s853:
        c.f.s   $fcc3, $f8, $f10                # 460a4330
1:
        .set pop
        .set push
        .set mips1
s854:
        c.f.d   $f2, $f4                        # 46241030
1:
        .set pop
        .set push
        .set mips1
s855:
        c.f.d   $f4, $f6                        # 46262030
1:
        .set pop
        .set push
        .set mips1
s856:
        c.f.d   $f6, $f8                        # 46283030
1:
        .set pop
        .set push
        .set mips4
s857:
        c.f.d   $fcc1, $f4, $f6                 # 46262130
1:
        .set pop
        .set push
        .set mips4
s858:
        c.f.d   $fcc2, $f6, $f8                 # 46283230
1:
        .set pop
        .set push
        .set mips4
s859:
        c.f.d   $fcc3, $f8, $f10                # 462a4330
1:
        .set pop
        .set push
        .set mips1
s860:
        c.ueq.s $f2, $f4                        # 46041033
1:
        .set pop
        .set push
        .set mips1
s861:
        c.ueq.s $f4, $f6                        # 46062033
1:
        .set pop
        .set push
        .set mips1
s862:
        c.ueq.s $f6, $f8                        # 46083033
1:
        .set pop
        .set push
        .set mips4
s863:
        c.ueq.s $fcc1, $f4, $f6                 # 46062133
1:
        .set pop
        .set push
        .set mips4
s864:
        c.ueq.s $fcc2, $f6, $f8                 # 46083233
1:
        .set pop
        .set push
        .set mips4
s865:
        c.ueq.s $fcc3, $f8, $f10                # 460a4333
1:
        .set pop
        .set push
        .set mips1
s866:
        c.ueq.d $f2, $f4                        # 46241033
1:
        .set pop
        .set push
        .set mips1
s867:
        c.ueq.d $f4, $f6                        # 46262033
1:
        .set pop
        .set push
        .set mips1
s868:
        c.ueq.d $f6, $f8                        # 46283033
1:
        .set pop
        .set push
        .set mips4
s869:
        c.ueq.d $fcc1, $f4, $f6                 # 46262133
1:
        .set pop
        .set push
        .set mips4
s870:
        c.ueq.d $fcc2, $f6, $f8                 # 46283233
1:
        .set pop
        .set push
        .set mips4
s871:
        c.ueq.d $fcc3, $f8, $f10                # 462a4333
1:
        .set pop
        .set push
        .set mips1
s872:
        c.ult.s $f2, $f4                        # 46041035
1:
        .set pop
        .set push
        .set mips1
s873:
        c.ult.s $f4, $f6                        # 46062035
1:
        .set pop
        .set push
        .set mips1
s874:
        c.ult.s $f6, $f8                        # 46083035
1:
        .set pop
        .set push
        .set mips4
s875:
        c.ult.s $fcc1, $f4, $f6                 # 46062135
1:
        .set pop
        .set push
        .set mips4
s876:
        c.ult.s $fcc2, $f6, $f8                 # 46083235
1:
        .set pop
        .set push
        .set mips4
s877:
        c.ult.s $fcc3, $f8, $f10                # 460a4335
1:
        .set pop
        .set push
        .set mips1
s878:
        c.ult.d $f2, $f4                        # 46241035
1:
        .set pop
        .set push
        .set mips1
s879:
        c.ult.d $f4, $f6                        # 46262035
1:
        .set pop
        .set push
        .set mips1
s880:
        c.ult.d $f6, $f8                        # 46283035
1:
        .set pop
        .set push
        .set mips4
s881:
        c.ult.d $fcc1, $f4, $f6                 # 46262135
1:
        .set pop
        .set push
        .set mips4
s882:
        c.ult.d $fcc2, $f6, $f8                 # 46283235
1:
        .set pop
        .set push
        .set mips4
s883:
        c.ult.d $fcc3, $f8, $f10                # 462a4335
1:
        .set pop
        .set push
        .set mips1
s884:
        c.ole.s $f2, $f4                        # 46041036
1:
        .set pop
        .set push
        .set mips1
s885:
        c.ole.s $f4, $f6                        # 46062036
1:
        .set pop
        .set push
        .set mips1
s886:
        c.ole.s $f6, $f8                        # 46083036
1:
        .set pop
        .set push
        .set mips4
s887:
        c.ole.s $fcc1, $f4, $f6                 # 46062136
1:
        .set pop
        .set push
        .set mips4
s888:
        c.ole.s $fcc2, $f6, $f8                 # 46083236
1:
        .set pop
        .set push
        .set mips4
s889:
        c.ole.s $fcc3, $f8, $f10                # 460a4336
1:
        .set pop
        .set push
        .set mips1
s890:
        c.ole.d $f2, $f4                        # 46241036
1:
        .set pop
        .set push
        .set mips1
s891:
        c.ole.d $f4, $f6                        # 46262036
1:
        .set pop
        .set push
        .set mips1
s892:
        c.ole.d $f6, $f8                        # 46283036
1:
        .set pop
        .set push
        .set mips4
s893:
        c.ole.d $fcc1, $f4, $f6                 # 46262136
1:
        .set pop
        .set push
        .set mips4
s894:
        c.ole.d $fcc2, $f6, $f8                 # 46283236
1:
        .set pop
        .set push
        .set mips4
s895:
        c.ole.d $fcc3, $f8, $f10                # 462a4336
1:
        .set pop
        .set push
        .set mips1
s896:
        c.ule.s $f2, $f4                        # 46041037
1:
        .set pop
        .set push
        .set mips1
s897:
        c.ule.s $f4, $f6                        # 46062037
1:
        .set pop
        .set push
        .set mips1
s898:
        c.ule.s $f6, $f8                        # 46083037
1:
        .set pop
        .set push
        .set mips4
s899:
        c.ule.s $fcc1, $f4, $f6                 # 46062137
1:
        .set pop
        .set push
        .set mips4
s900:
        c.ule.s $fcc2, $f6, $f8                 # 46083237
1:
        .set pop
        .set push
        .set mips4
s901:
        c.ule.s $fcc3, $f8, $f10                # 460a4337
1:
        .set pop
        .set push
        .set mips1
s902:
        c.ule.d $f2, $f4                        # 46241037
1:
        .set pop
        .set push
        .set mips1
s903:
        c.ule.d $f4, $f6                        # 46262037
1:
        .set pop
        .set push
        .set mips1
s904:
        c.ule.d $f6, $f8                        # 46283037
1:
        .set pop
        .set push
        .set mips4
s905:
        c.ule.d $fcc1, $f4, $f6                 # 46262137
1:
        .set pop
        .set push
        .set mips4
s906:
        c.ule.d $fcc2, $f6, $f8                 # 46283237
1:
        .set pop
        .set push
        .set mips4
s907:
        c.ule.d $fcc3, $f8, $f10                # 462a4337
1:
        .set pop
        .set push
        .set mips1
s908:
        c.sf.s  $f2, $f4                        # 46041038
1:
        .set pop
        .set push
        .set mips1
s909:
        c.sf.s  $f4, $f6                        # 46062038
1:
        .set pop
        .set push
        .set mips1
s910:
        c.sf.s  $f6, $f8                        # 46083038
1:
        .set pop
        .set push
        .set mips4
s911:
        c.sf.s  $fcc1, $f4, $f6                 # 46062138
1:
        .set pop
        .set push
        .set mips4
s912:
        c.sf.s  $fcc2, $f6, $f8                 # 46083238
1:
        .set pop
        .set push
        .set mips4
s913:
        c.sf.s  $fcc3, $f8, $f10                # 460a4338
1:
        .set pop
        .set push
        .set mips1
s914:
        c.sf.d  $f2, $f4                        # 46241038
1:
        .set pop
        .set push
        .set mips1
s915:
        c.sf.d  $f4, $f6                        # 46262038
1:
        .set pop
        .set push
        .set mips1
s916:
        c.sf.d  $f6, $f8                        # 46283038
1:
        .set pop
        .set push
        .set mips4
s917:
        c.sf.d  $fcc1, $f4, $f6                 # 46262138
1:
        .set pop
        .set push
        .set mips4
s918:
        c.sf.d  $fcc2, $f6, $f8                 # 46283238
1:
        .set pop
        .set push
        .set mips4
s919:
        c.sf.d  $fcc3, $f8, $f10                # 462a4338
1:
        .set pop
        .set push
        .set mips1
s920:
        c.ngle.s $f2, $f4                       # 46041039
1:
        .set pop
        .set push
        .set mips1
s921:
        c.ngle.s $f4, $f6                       # 46062039
1:
        .set pop
        .set push
        .set mips1
s922:
        c.ngle.s $f6, $f8                       # 46083039
1:
        .set pop
        .set push
        .set mips4
s923:
        c.ngle.s $fcc1, $f4, $f6                # 46062139
1:
        .set pop
        .set push
        .set mips4
s924:
        c.ngle.s $fcc2, $f6, $f8                # 46083239
1:
        .set pop
        .set push
        .set mips4
s925:
        c.ngle.s $fcc3, $f8, $f10               # 460a4339
1:
        .set pop
        .set push
        .set mips1
s926:
        c.ngle.d $f2, $f4                       # 46241039
1:
        .set pop
        .set push
        .set mips1
s927:
        c.ngle.d $f4, $f6                       # 46262039
1:
        .set pop
        .set push
        .set mips1
s928:
        c.ngle.d $f6, $f8                       # 46283039
1:
        .set pop
        .set push
        .set mips4
s929:
        c.ngle.d $fcc1, $f4, $f6                # 46262139
1:
        .set pop
        .set push
        .set mips4
s930:
        c.ngle.d $fcc2, $f6, $f8                # 46283239
1:
        .set pop
        .set push
        .set mips4
s931:
        c.ngle.d $fcc3, $f8, $f10               # 462a4339
1:
        .set pop
        .set push
        .set mips1
s932:
        c.seq.s $f2, $f4                        # 4604103a
1:
        .set pop
        .set push
        .set mips1
s933:
        c.seq.s $f4, $f6                        # 4606203a
1:
        .set pop
        .set push
        .set mips1
s934:
        c.seq.s $f6, $f8                        # 4608303a
1:
        .set pop
        .set push
        .set mips4
s935:
        c.seq.s $fcc1, $f4, $f6                 # 4606213a
1:
        .set pop
        .set push
        .set mips4
s936:
        c.seq.s $fcc2, $f6, $f8                 # 4608323a
1:
        .set pop
        .set push
        .set mips4
s937:
        c.seq.s $fcc3, $f8, $f10                # 460a433a
1:
        .set pop
        .set push
        .set mips1
s938:
        c.seq.d $f2, $f4                        # 4624103a
1:
        .set pop
        .set push
        .set mips1
s939:
        c.seq.d $f4, $f6                        # 4626203a
1:
        .set pop
        .set push
        .set mips1
s940:
        c.seq.d $f6, $f8                        # 4628303a
1:
        .set pop
        .set push
        .set mips4
s941:
        c.seq.d $fcc1, $f4, $f6                 # 4626213a
1:
        .set pop
        .set push
        .set mips4
s942:
        c.seq.d $fcc2, $f6, $f8                 # 4628323a
1:
        .set pop
        .set push
        .set mips4
s943:
        c.seq.d $fcc3, $f8, $f10                # 462a433a
1:
        .set pop
        .set push
        .set mips1
s944:
        c.ngl.s $f2, $f4                        # 4604103b
1:
        .set pop
        .set push
        .set mips1
s945:
        c.ngl.s $f4, $f6                        # 4606203b
1:
        .set pop
        .set push
        .set mips1
s946:
        c.ngl.s $f6, $f8                        # 4608303b
1:
        .set pop
        .set push
        .set mips4
s947:
        c.ngl.s $fcc1, $f4, $f6                 # 4606213b
1:
        .set pop
        .set push
        .set mips4
s948:
        c.ngl.s $fcc2, $f6, $f8                 # 4608323b
1:
        .set pop
        .set push
        .set mips4
s949:
        c.ngl.s $fcc3, $f8, $f10                # 460a433b
1:
        .set pop
        .set push
        .set mips1
s950:
        c.ngl.d $f2, $f4                        # 4624103b
1:
        .set pop
        .set push
        .set mips1
s951:
        c.ngl.d $f4, $f6                        # 4626203b
1:
        .set pop
        .set push
        .set mips1
s952:
        c.ngl.d $f6, $f8                        # 4628303b
1:
        .set pop
        .set push
        .set mips4
s953:
        c.ngl.d $fcc1, $f4, $f6                 # 4626213b
1:
        .set pop
        .set push
        .set mips4
s954:
        c.ngl.d $fcc2, $f6, $f8                 # 4628323b
1:
        .set pop
        .set push
        .set mips4
s955:
        c.ngl.d $fcc3, $f8, $f10                # 462a433b
1:
        .set pop
        .set push
        .set mips1
s956:
        c.nge.s $f2, $f4                        # 4604103d
1:
        .set pop
        .set push
        .set mips1
s957:
        c.nge.s $f4, $f6                        # 4606203d
1:
        .set pop
        .set push
        .set mips1
s958:
        c.nge.s $f6, $f8                        # 4608303d
1:
        .set pop
        .set push
        .set mips4
s959:
        c.nge.s $fcc1, $f4, $f6                 # 4606213d
1:
        .set pop
        .set push
        .set mips4
s960:
        c.nge.s $fcc2, $f6, $f8                 # 4608323d
1:
        .set pop
        .set push
        .set mips4
s961:
        c.nge.s $fcc3, $f8, $f10                # 460a433d
1:
        .set pop
        .set push
        .set mips1
s962:
        c.nge.d $f2, $f4                        # 4624103d
1:
        .set pop
        .set push
        .set mips1
s963:
        c.nge.d $f4, $f6                        # 4626203d
1:
        .set pop
        .set push
        .set mips1
s964:
        c.nge.d $f6, $f8                        # 4628303d
1:
        .set pop
        .set push
        .set mips4
s965:
        c.nge.d $fcc1, $f4, $f6                 # 4626213d
1:
        .set pop
        .set push
        .set mips4
s966:
        c.nge.d $fcc2, $f6, $f8                 # 4628323d
1:
        .set pop
        .set push
        .set mips4
s967:
        c.nge.d $fcc3, $f8, $f10                # 462a433d
1:
        .set pop
        .set push
        .set mips1
s968:
        c.ngt.s $f2, $f4                        # 4604103f
1:
        .set pop
        .set push
        .set mips1
s969:
        c.ngt.s $f4, $f6                        # 4606203f
1:
        .set pop
        .set push
        .set mips1
s970:
        c.ngt.s $f6, $f8                        # 4608303f
1:
        .set pop
        .set push
        .set mips4
s971:
        c.ngt.s $fcc1, $f4, $f6                 # 4606213f
1:
        .set pop
        .set push
        .set mips4
s972:
        c.ngt.s $fcc2, $f6, $f8                 # 4608323f
1:
        .set pop
        .set push
        .set mips4
s973:
        c.ngt.s $fcc3, $f8, $f10                # 460a433f
1:
        .set pop
        .set push
        .set mips1
s974:
        c.ngt.d $f2, $f4                        # 4624103f
1:
        .set pop
        .set push
        .set mips1
s975:
        c.ngt.d $f4, $f6                        # 4626203f
1:
        .set pop
        .set push
        .set mips1
s976:
        c.ngt.d $f6, $f8                        # 4628303f
1:
        .set pop
        .set push
        .set mips4
s977:
        c.ngt.d $fcc1, $f4, $f6                 # 4626213f
1:
        .set pop
        .set push
        .set mips4
s978:
        c.ngt.d $fcc2, $f6, $f8                 # 4628323f
1:
        .set pop
        .set push
        .set mips4
s979:
        c.ngt.d $fcc3, $f8, $f10                # 462a433f
1:
        .set pop
        .set push
        .set mips64r2
s980:
        dsbh    $2, $3                          # 7c0310a4
1:
        .set pop
        .set push
        .set mips64r2
s981:
        dsbh    $3, $4                          # 7c0418a4
1:
        .set pop
        .set push
        .set mips64r2
s982:
        dsbh    $4, $5                          # 7c0520a4
1:
        .set pop
        .set push
        .set mips64r2
s983:
        dshd    $2, $3                          # 7c031164
1:
        .set pop
        .set push
        .set mips64r2
s984:
        dshd    $3, $4                          # 7c041964
1:
        .set pop
        .set push
        .set mips64r2
s985:
        dshd    $4, $5                          # 7c052164
1:
        .set pop
        .set push
        .set mips64r2
s986:
        drotr   $2, $3, 5                       # 0023117a
1:
        .set pop
        .set push
        .set mips64r2
s987:
        drotr   $3, $4, 1                       # 0024187a
1:
        .set pop
        .set push
        .set mips64r2
s988:
        drotr   $4, $5, 63                      # 002527fe
1:
        .set pop
        .set push
        .set mips64r2
s989:
        drotr32 $2, $3, 5                       # 0023117e
1:
        .set pop
        .set push
        .set mips64r2
s990:
        drotr32 $3, $4, 1                       # 0024187e
1:
        .set pop
        .set push
        .set mips64r2
s991:
        drotr32 $4, $5, 31                      # 002527fe
1:
        .set pop
        .set push
        .set mips64r2
s992:
        drotrv  $2, $3, $4                      # 00831056
1:
        .set pop
        .set push
        .set mips64r2
s993:
        drotrv  $3, $4, $5                      # 00a41856
1:
        .set pop
        .set push
        .set mips64r2
s994:
        drotrv  $4, $5, $6                      # 00c52056
1:
        .set pop
        .set push
        .set mips64r2
s995:
        dext    $2, $3, 4, 8                    # 7c623903
1:
        .set pop
        .set push
        .set mips64r2
s996:
        dext    $3, $4, 0, 32                   # 7c83f803
1:
        .set pop
        .set push
        .set mips64r2
s997:
        dext    $4, $5, 31, 32                  # 7ca4ffc3
1:
        .set pop
        .set push
        .set mips64r2
s998:
        dext    $2, $3, 4, 40                   # 7c623901
1:
        .set pop
        .set push
        .set mips64r2
s999:
        dext    $3, $4, 0, 64                   # 7c83f801
1:
        .set pop
        .set push
        .set mips64r2
s1000:
        dext    $4, $5, 31, 33                  # 7ca407c1
1:
        .set pop
        .set push
        .set mips64r2
s1001:
        dext    $2, $3, 36, 8                   # 7c623902
1:
        .set pop
        .set push
        .set mips64r2
s1002:
        dext    $3, $4, 32, 32                  # 7c83f802
1:
        .set pop
        .set push
        .set mips64r2
s1003:
        dext    $4, $5, 63, 1                   # 7ca407c2
1:
        .set pop
        .set push
        .set mips64r2
s1004:
        dextm   $2, $3, 4, 40                   # 7c623901
1:
        .set pop
        .set push
        .set mips64r2
s1005:
        dextm   $3, $4, 0, 64                   # 7c83f801
1:
        .set pop
        .set push
        .set mips64r2
s1006:
        dextm   $4, $5, 31, 33                  # 7ca407c1
1:
        .set pop
        .set push
        .set mips64r2
s1007:
        dextu   $2, $3, 36, 8                   # 7c623902
1:
        .set pop
        .set push
        .set mips64r2
s1008:
        dextu   $3, $4, 32, 32                  # 7c83f802
1:
        .set pop
        .set push
        .set mips64r2
s1009:
        dextu   $4, $5, 63, 1                   # 7ca407c2
1:
        .set pop
        .set push
        .set mips64r2
s1010:
        dins    $2, $3, 4, 8                    # 7c625907
1:
        .set pop
        .set push
        .set mips64r2
s1011:
        dins    $3, $4, 0, 32                   # 7c83f807
1:
        .set pop
        .set push
        .set mips64r2
s1012:
        dins    $4, $5, 31, 1                   # 7ca4ffc7
1:
        .set pop
        .set push
        .set mips64r2
s1013:
        dins    $2, $3, 4, 40                   # 7c625905
1:
        .set pop
        .set push
        .set mips64r2
s1014:
        dins    $3, $4, 0, 64                   # 7c83f805
1:
        .set pop
        .set push
        .set mips64r2
s1015:
        dins    $4, $5, 31, 2                   # 7ca407c5
1:
        .set pop
        .set push
        .set mips64r2
s1016:
        dins    $2, $3, 36, 8                   # 7c625906
1:
        .set pop
        .set push
        .set mips64r2
s1017:
        dins    $3, $4, 32, 32                  # 7c83f806
1:
        .set pop
        .set push
        .set mips64r2
s1018:
        dins    $4, $5, 63, 1                   # 7ca4ffc6
1:
        .set pop
        .set push
        .set mips64r2
s1019:
        dinsm   $2, $3, 4, 40                   # 7c625905
1:
        .set pop
        .set push
        .set mips64r2
s1020:
        dinsm   $3, $4, 0, 64                   # 7c83f805
1:
        .set pop
        .set push
        .set mips64r2
s1021:
        dinsm   $4, $5, 31, 2                   # 7ca407c5
1:
        .set pop
        .set push
        .set mips64r2
s1022:
        dinsu   $2, $3, 36, 8                   # 7c625906
1:
        .set pop
        .set push
        .set mips64r2
s1023:
        dinsu   $3, $4, 32, 32                  # 7c83f806
1:
        .set pop
        .set push
        .set mips64r2
s1024:
        dinsu   $4, $5, 63, 1                   # 7ca4ffc6
1:
        .set pop
end:
