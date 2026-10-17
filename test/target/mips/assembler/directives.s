# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Data, layout, symbol, conditional, and macro directives; directives.cmake
# lists the bytes and symbols they produce, which GNU as also produces.
        .set noreorder
        .text
        .globl start
start:
        nop
        .balign 16, 0
here:
        .fill 3, 2, 0x1234
        .skip 2
        .p2align 3
        .ascii "ab", "cd"
        .asciz "x\ty\n\101\x42"
        .string "s"
        .float 1.5, -2.25
        .double 3.0e10
        .p2align 2
        .sleb128 -129, 63, -64
        .uleb128 .Lend - .Lstart, 300
.Lstart:
        .rept 3
        .byte 0x7f
        .endr
.Lend:
        .byte .Lend - .Lstart
count = 5
        .set step, count * 2
        .equiv twice, step + step
        .word count, step, twice, (1 << 4) | 3, 7 % 4, ~0, -(3), !0, 6 / 4
        .ifdef count
        .word 1
        .elseif 1
        .word 2
        .else
        .word 3
        .endif
        .ifndef nothing
        .word 4
        .endif
        .if count > 4 && step == 10
        .word 5
        .endif
        .macro pair a, b=9
        .word \a, \b
        .endm
        pair 1
        pair 2, 3
        pair b=4, a=5
1:      .word 1b, 2f
2:      .comm common_thing, 16, 8
        .lcomm local_thing, 12
        .ident "Cross test"
        .weak weakling
        .word weakling
        .data
        .globl alias
        .set alias, here
        .word alias
        .section .note.test,"",@note
        .word 1
        .section .init_array,"aw"
        .word start
