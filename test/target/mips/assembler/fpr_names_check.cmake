# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The FPR mask of .reginfo holds the `$fN` operands only, a double's pair
# with 32-bit FPRs: $f0, $f2, $f4, $f8, and $f9.
section_words("${OBJECT}" .reginfo big reginfo)
list(GET reginfo 2 fprs)
if(NOT fprs STREQUAL "00000315")
    message(FATAL_ERROR ".reginfo FPR mask is ${fprs}, expected 00000315")
endif()
