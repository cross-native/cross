# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `as` encodes every instruction and macro of encodings.s to the words in the
# comments, in both byte orders. With mips64-elf-as installed, GNU as must
# produce the same words.

foreach(required AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

commented_words("${SOURCE}" expected)
list(LENGTH expected count)
if(count LESS 200)
    message(FATAL_ERROR "encodings.s lists only ${count} words")
endif()

foreach(order big little)
    set(object "${OUTPUT}.${order}.o")
    set(endian -EB)
    if(order STREQUAL "little")
        set(endian -EL)
    endif()
    run_checked("${order}-endian assembly" "${AS}" -target mips-unknown-elf -march=mips64
                ${endian} "${SOURCE}" -o "${object}")
    text_words("${object}" ${order} words)
    if(NOT words STREQUAL expected)
        message(FATAL_ERROR "${order}-endian words differ\nexpected: ${expected}\nactual:   ${words}")
    endif()
endforeach()

find_program(GNU_AS NAMES mips64-elf-as)
if(GNU_AS)
    run_checked("GNU as" "${GNU_AS}" -EB -march=mips64r2 -mabi=32 "${SOURCE}"
                -o "${OUTPUT}.gnu.o")
    text_words("${OUTPUT}.gnu.o" big words)
    if(NOT words STREQUAL expected)
        message(FATAL_ERROR "GNU as encodes differently\nexpected: ${expected}\nGNU as:   ${words}")
    endif()
endif()
message(STATUS "${count} words matched")
