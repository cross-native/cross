# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Subsection 1 of .text, written first, follows subsection 0, as `later`
# and the .data relocation show.
section_words("${OBJECT}" .table big table)
expect_lines(".table words" "${table}" "a0a11011;20213031;0a04")
execute_process(COMMAND "${LLVM_READELF}" -s --wide "${OBJECT}" OUTPUT_VARIABLE symbols)
if(NOT symbols MATCHES "00000008 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ later\n")
    message(FATAL_ERROR "'later' does not follow subsection 0\n${symbols}")
endif()
relocation_lines("${OBJECT}" relocations)
expect_lines(relocations "${relocations}" ".rel.data 0 R_MIPS_32 .text")
section_words("${OBJECT}" .data big data)
expect_lines(".data words" "${data}" "00000008")
text_words("${OBJECT}" big words)
expect_lines(".text words" "${words}" "03e00008;00000000;00000000")
