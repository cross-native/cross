# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Relocation operators, jumps, branches, and data references to global,
# local, undefined, and mergeable symbols. The comments list the encoded
# words, whose 16- and 26-bit fields carry the in-place addends of a REL
# object; relocations-o32.txt and relocations-n64.txt list the
# relocations.
        .set noreorder
        .set noat
        .text
        .globl func
func:
        lui $2, %hi(data_object)         # 3c020000
        addiu $2, $2, %lo(data_object)   # 24420000
        lui $3, %hi(local_object+8)      # 3c030000
        lw $3, %lo(local_object+8)($3)   # 8c630008
        lw $4, %gp_rel(small)($28)       # 8f840000
        jal external                     # 0c000000
        nop                              # 00000000
        jal func                         # 0c000000
        nop                              # 00000000
        j .Llocal                        # 0800000f
        nop                              # 00000000
        bal external_branch              # 0411ffff
        nop                              # 00000000
        addiu $6, $6, %lo(late)          # 24c60000
        lui $6, %hi(late)                # 3c060000
.Llocal:
        lui $5, %hi(.Lstring)            # 3c050000
        addiu $5, $5, %lo(.Lstring)      # 24a50000
        jr $31                           # 03e00008
        nop                              # 00000000

        .data
local_object:
        .word 1, 2, 3
        .globl data_object
data_object:
        .word func
        .word local_object+4
        .4byte .Lstring
        .4byte .Lstring+2
        .globl late
late:
        .word 0

        .section .sdata,"aw",@progbits
small:
        .word 5

        .section .rodata.str1.1,"aMS",@progbits,1
        .byte 0
.Lstring:
        .asciz "hello"
