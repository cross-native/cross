# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# relocations.s in an o32 object has the REL relocations of
# relocations-o32.txt, HI16 before its LO16, and the in-place addends in its
# comments and data; in an n64 object it has the RELA relocations of
# relocations-n64.txt.

foreach(required AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

function(expected_lines file result)
    file(STRINGS "${file}" lines REGEX "^[^#]")
    set(${result} "${lines}" PARENT_SCOPE)
endfunction()

get_filename_component(directory "${SOURCE}" DIRECTORY)
run_checked("o32 assembly" "${AS}" -target mips-unknown-elf "${SOURCE}" -o "${OUTPUT}.o32.o")
relocation_lines("${OUTPUT}.o32.o" actual)
expected_lines("${directory}/relocations-o32.txt" expected)
expect_lines("o32 relocations" "${actual}" "${expected}")
commented_words("${SOURCE}" expected)
text_words("${OUTPUT}.o32.o" big actual)
expect_lines("o32 instruction words" "${actual}" "${expected}")
section_words("${OUTPUT}.o32.o" .data big actual)
expect_lines("o32 data words" "${actual}"
             "00000001;00000002;00000003;00000000;00000004;00000001;00000002;00000000")

run_checked("n64 assembly" "${AS}" -target mips64-unknown-elf "${SOURCE}" -o "${OUTPUT}.n64.o")
relocation_lines("${OUTPUT}.n64.o" actual)
expected_lines("${directory}/relocations-n64.txt" expected)
expect_lines("n64 relocations" "${actual}" "${expected}")
section_words("${OUTPUT}.n64.o" .data big actual)
expect_lines("n64 data words" "${actual}"
             "00000001;00000002;00000003;00000000;00000000;00000000;00000000;00000000")
