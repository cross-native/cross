# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# SOURCE assembled with OPTIONS gives the .text words its comments list, if
# any, read in ORDER (big by default). With mips64-elf-as installed, GNU as
# given GNU_OPTIONS produces an object with the same sections, relocations,
# and symbols, or with GNU_WORDS set, the same .text words. CHECK names an
# optional script run afterwards with OBJECT set.

foreach(required AS SOURCE OUTPUT OPTIONS GNU_OPTIONS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be set")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()
if(NOT DEFINED ORDER)
    set(ORDER big)
endif()

commented_words("${SOURCE}" expected)
separate_arguments(options UNIX_COMMAND "${OPTIONS}")
set(OBJECT "${OUTPUT}.o")
run_checked("assembly" "${AS}" ${options} "${SOURCE}" -o "${OBJECT}")
if(expected)
    text_words("${OBJECT}" ${ORDER} words)
    expect_lines(".text words" "${words}" "${expected}")
endif()
if(GNU_WORDS AND GNU_AS)
    separate_arguments(gnu_options UNIX_COMMAND "${GNU_OPTIONS}")
    run_checked("GNU as" "${GNU_AS}" ${gnu_options} "${SOURCE}" -o "${OUTPUT}.gnu.o")
    text_words("${OUTPUT}.gnu.o" ${ORDER} gnu_words)
    expect_lines("GNU as .text words" "${gnu_words}" "${expected}")
elseif(NOT GNU_WORDS)
    compare_with_gnu("${SOURCE}" "${SOURCE}" "${OPTIONS}" "${GNU_OPTIONS}")
endif()
if(DEFINED CHECK)
    include("${CHECK}")
endif()
list(LENGTH expected count)
message(STATUS "${count} words matched")
