# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Expressions that only layout settles, as GNU as resolves them: label
# differences across alignment and fill in instruction immediates, li, data,
# fill counts, and symbol assignments; and macro arguments, quoted and, in
# alternate mode, `%expr` values. The code section ends padded to its
# alignment; deferred_check.cmake lists the .data words.
        .set noreorder
        .text
a:
        nop                             # 00000000 00000000 00000000 00000000
        .balign 16
b:
        nop                             # 00000000
        addiu   $3, $0, b - a           # 24030010
        lui     $4, (b - a) >> 2        # 3c040004
        li      $5, c - b               # 24050014
        ori     $6, $0, c - a           # 34060024
c:
        jr      $31                     # 03e00008
        nop                             # 00000000 00000000

        .data
d0:
        .word   (b - a) >> 2, c - b, e - d0
        .space  (b - a) / 8
        .set    n, e - d0
        .byte   n
        .balign 4
e:
        .macro  text value
        .ascii  "\value"
        .endm
        text    "ok"
        .altmacro
        .macro  number value
        .byte   value
        .endm
        number  %(3 * 4)
        .noaltmacro
        .set    base, 0x1e0000
        .if     base & ((1 << 11) - 1)
        .error  "base is not aligned to 2 KiB"
        .endif
