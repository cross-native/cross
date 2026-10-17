# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A %gp_rel of a local symbol farther than 32 KiB into its section names the
# symbol, so its o32 in-place addend fits; `.2byte symbol` in an n64 object
# is R_MIPS_REL16, as GNU as writes it (o32 has no 2-byte data relocation).

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

set(source "${OUTPUT}.gprel.s")
file(WRITE "${source}" "        .set noreorder\n        .data\n        .space 0x10000\nfar:    .word 0\nnear:   .word 1\n        .text\n        lw $2, %gp_rel(far)($28)\n        lw $3, %gp_rel(far+4)($28)\n        lw $4, %gp_rel(near-0x10000)($28)\n")
run_checked("gp_rel" "${AS}" -target mips-unknown-elf "${source}" -o "${OUTPUT}.gprel.o")
relocation_lines("${OUTPUT}.gprel.o" relocations)
expect_lines("gp_rel relocations" "${relocations}"
    ".rel.text 0 R_MIPS_GPREL16 far;.rel.text 4 R_MIPS_GPREL16 far;.rel.text 8 R_MIPS_GPREL16 .data")
text_words("${OUTPUT}.gprel.o" big words)
expect_lines("gp_rel words" "${words}" "8f820000;8f830004;8f840004")

set(source "${OUTPUT}.half.s")
file(WRITE "${source}" "        .data\nx:      .word 0\n        .2byte x\n        .2byte x+2\n        .half y\n")
run_checked("n64 .2byte" "${AS}" -target mips64-unknown-elf "${source}" -o "${OUTPUT}.half.o")
relocation_lines("${OUTPUT}.half.o" relocations)
expect_lines("n64 .2byte relocations" "${relocations}"
    ".rela.data 4 R_MIPS_REL16 .data + 0;.rela.data 6 R_MIPS_REL16 .data + 2;.rela.data 8 R_MIPS_REL16 y + 0")
compare_with_gnu("n64 .2byte" "${source}" "-target mips64-unknown-elf" "-EB -64 -march=mips64")
