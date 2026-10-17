# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# GNU ld's expression values inside output sections (relative values as
# offsets in their section, plain numbers relative to it) and its rules for
# empty output sections: what keeps one, what leaves it out, and when a
# section of assignments alone is allocated.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
run_checked("assemble" "${AS}" -target mips-unknown-elf "${sources}/expressions.s" -o "${OUTPUT}/expressions.o")
set(image "${OUTPUT}/expressions.elf")
run_checked("link" "${LD}" -T "${sources}/expressions.ld" "${OUTPUT}/expressions.o" -o "${image}")
run_checked("headers" "${LLVM_READELF}" -S -l -W "${image}")
set(headers "${run_stdout}")
# The zero-filled .pad shares the code's page and so its segment.
expect("headers" "${headers}"
       "[.]kept +PROGBITS +80100004 [0-9a-f]+ 000000 00 +A "
       "[.]pad +NOBITS +80100004 [0-9a-f]+ 00000c 00 +WA "
       "[.]moved +PROGBITS +80100010 [0-9a-f]+ 000000 00 +W "
       "[.]data +PROGBITS +80200004 [0-9a-f]+ 000012 "
       "[.]bss +NOBITS +80200018 [0-9a-f]+ 000008 "
       "LOAD +0x[0-9a-f]+ 0x80100000 0x80100000 0x00004 0x00010 RWE ")
foreach(absent plus cond loose marks lbss)
    if(headers MATCHES "[.]${absent} ")
        message(FATAL_ERROR "the image keeps .${absent}\n${headers}")
    endif()
endforeach()
run_checked("symbols" "${LLVM_NM}" --format=posix "${image}")
expect("symbols" "${run_stdout}" "(^|\n)mark . 80100010 " "(^|\n)data_end D 80200016 "
       "(^|\n)aligned D 8020001c " "(^|\n)offset D 80200024 " "(^|\n)distance D 8030001a ")
if(GNU_LD)
    run_checked("GNU ld" "${GNU_LD}" -T "${sources}/expressions.ld" "${OUTPUT}/expressions.o"
                -o "${OUTPUT}/gnu.elf")
    same_as_gnu("expressions" "${image}" "${OUTPUT}/gnu.elf")
endif()
