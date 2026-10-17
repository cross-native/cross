# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The definitions relocations_o32.s and relocations_n64.s refer to.

        .set    noreorder
        .text
        .globl  far_function
        .globl  elsewhere
far_function:
        jr      $31
        nop
elsewhere:
        jr      $31
        nop

        .data
        .globl  table
table:
        .space  0x10020

        .sdata
        .globl  small
small:
        .word   3
