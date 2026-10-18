# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `as` encodes each form of the shipped instruction table, under the ISA
# level that has it, to the words in the comments of table.s, in both byte
# orders.

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
foreach(order big little)
    set(endian -EB)
    if(order STREQUAL "little")
        set(endian -EL)
    endif()
    run_checked("${order}-endian assembly" "${AS}" -target mips-unknown-elf -march=mips64r2
                ${endian} "${SOURCE}" -o "${OUTPUT}.${order}.o")
    text_words("${OUTPUT}.${order}.o" ${order} words)
    expect_lines("${order}-endian words" "${words}" "${expected}")
endforeach()
list(LENGTH expected count)
message(STATUS "${count} words matched")
