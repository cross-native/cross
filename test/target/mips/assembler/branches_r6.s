# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Release 6 branches to labels: the delay-slot branches and BC1EQZ/BC1NEZ,
# every compact branch, with BEQC, BNEC, BOVC, and BNVC registers in either
# order, BC and BALC to an external symbol, and the PC-relative LAPC and
# LWPC. As GNU as does on Release 6, every PC-relative field keeps a
# relocation against its own label, also within its section.
        .set noreorder
        .text
back:
        beq     $4, $5, back            # 1085ffff
        bne     $4, $5, .Lfwd           # 1485ffff
        bc1eqz  $f2, back               # 4522ffff
        bc1nez  $f31, .Lfwd             # 45bfffff
        bal     back                    # 0411ffff
        beqc    $4, $5, back            # 2085ffff
        beqc    $5, $4, back            # 2085ffff
        bnec    $6, $7, .Lfwd           # 60c7ffff
        bovc    $4, $5, back            # 20a4ffff
        bovc    $0, $4, back            # 2080ffff
        bnvc    $5, $4, .Lfwd           # 60a4ffff
        bltc    $4, $5, back            # 5c85ffff
        bltc    $5, $4, back            # 5ca4ffff
        bgec    $5, $4, .Lfwd           # 58a4ffff
        bltuc   $5, $4, back            # 1ca4ffff
        bgeuc   $5, $4, .Lfwd           # 18a4ffff
        bltzc   $4, back                # 5c84ffff
        bgezc   $4, back                # 5884ffff
        blezc   $4, .Lfwd               # 5804ffff
        bgtzc   $4, .Lfwd               # 5c04ffff
        beqzc   $4, back                # d89fffff
        bnezc   $4, .Lfwd               # f89fffff
        beqzalc $4, back                # 2004ffff
        bnezalc $4, back                # 6004ffff
        blezalc $4, back                # 1804ffff
        bgezalc $4, back                # 1884ffff
        bgtzalc $4, .Lfwd               # 1c04ffff
        bltzalc $4, .Lfwd               # 1c84ffff
        bc      back                    # cbffffff
        bc      external                # cbffffff
        balc    external                # ebffffff
        balc    .Lfwd                   # ebffffff
        addiupc $2, 8                   # ec400002
        lapc    $3, back                # ec600000
        lwpc    $4, data                # ec880000
.Lfwd:
        nop                             # 00000000
        .data
data:   .word   0
