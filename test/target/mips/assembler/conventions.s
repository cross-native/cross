# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# GNU as's object conventions for embedded MIPS ELF, which `as` follows for
# its input: section alignment from the contents, a code section padded to
# its alignment, .reginfo outside the image, .pdr and .gnu.attributes, $gp
# sections, GCC's private `$L` labels, the file symbol of the first .file,
# no line table without .loc rows, data that aligns itself and the labels
# before it, .align that moves them too, `.align 0`, .lcomm alignment,
# pointer-array entry sizes, and unmatched %hi relocations placed before
# their %lo.
        .file   1 "conventions.c"
        .section .mdebug.abi32
        .previous
        .nan    legacy
        .module fp=32
        .module nooddspreg
        .set    noreorder
        .section .text.f,"ax",@progbits
        .align  4
        .globl  f
        .ent    f
        .type   f, @function
f:
        .frame  $sp,0,$31
        .mask   0x00000000,0
        .fmask  0x00000000,0
        lui     $2, %hi(table)
        lui     $3, %hi(table)
        b       $L2
        lw      $4, %lo(table)($2)
$L2:
        jr      $31
        lw      $5, %lo(table)($3)
        .end    f
        .size   f, .-f
        .section .sdata.small,"aw"
        .byte   1
label1:
        .word   2
        .data
        .byte   3
label2:
        .align  2
        .word   4
        .align  0
        .byte   5
        .half   6
        .section .rodata.r,"a"
        .float  1.0
        .double 2.0
        .lcomm  buffer, 12
        .section .init_array,"aw"
        .word   f
