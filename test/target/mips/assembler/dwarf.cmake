# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# dwarf.s gives the same DWARF 5 line rows and call-frame rows in a big-endian
# o32 and a little-endian n64 object, and the object passes
# `llvm-dwarfdump --verify`.

foreach(required AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump)
if(ASSEMBLER_TEST_SKIP OR NOT LLVM_DWARFDUMP)
    message(STATUS "skipping assembler DWARF test: llvm-dwarfdump is required")
    return()
endif()

function(dump label object)
    execute_process(COMMAND "${LLVM_DWARFDUMP}" ${ARGN} "${object}"
                    RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${error}")
    endif()
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# Address, line, column, file, ISA, discriminator, and flags of each row.
set(rows
    "0x0+0 +3 +5 +0 +0 +0 +0 +is_stmt prologue_end\n"
    "0x0+8 +10 +2 +1 +0 +0 +0 +is_stmt\n"
    "0x0+10 +4 +1 +0 +0 +0 +0 +\n"
    "0x0+14 +4 +1 +0 +0 +0 +0 +basic_block\n"
    "0x0+14 +5 +1 +0 +0 +2 +0 +epilogue_begin\n"
    "0x0+1c +5 +1 +0 +0 +0 +0 +end_sequence\n"
    "0x0+0 +20 +0 +0 +0 +0 +0 +\n"
    "0x0+4 +2000 +7 +0 +0 +0 +0 +\n"
    "0x0+4b4 +21 +0 +0 +1 +0 +0 +\n"
    "0x0+4bc +21 +0 +0 +1 +0 +0 +end_sequence\n")
string(REPLACE ";" "" rows "${rows}")
set(frames
    "  0x0: CFA=SP_64\n  0x4: CFA=SP_64[+]16\n  0x8: CFA=SP_64[+]16: RA_64=[[]CFA-8[]]\n"
    "  0x14: CFA=SP_64[+]16\n  0x1c: CFA=SP_64: RA_64=[[]CFA-8[]]\n"
    "[^\n]*\n*[^\n]*FDE[^\n]*\n[^\n]*\n(.*\n)*  0x0: CFA=SP_64\n  0x4b4: CFA=FP_64\n")
string(REPLACE ";" "" frames "${frames}")

foreach(target mips-unknown-elf mips64el-unknown-elf)
    set(object "${OUTPUT}.${target}.o")
    run_checked("${target} assembly" "${AS}" -target ${target} "${SOURCE}" -o "${object}")
    dump("${target} verify" "${object}" --verify)
    expect("${target} verify" "${text}" "No errors[.]")
    dump("${target} lines" "${object}" --debug-line)
    expect("${target} lines" "${text}" "version: 5"
           "include_directories\\[ *0\\] = \"/work\""
           "include_directories\\[ *1\\] = \"/work/include\""
           "name: \"main.x\"\n +dir_index: 0" "name: \"header.x\"\n +dir_index: 1" "${rows}")
    dump("${target} frames" "${object}" --debug-frame)
    expect("${target} frames" "${text}" "Version: +4\n  Augmentation: +\"\"" "${frames}")
    dump("${target} eh frames" "${object}" --eh-frame)
    expect("${target} eh frames" "${text}" "Version: +1\n  Augmentation: +\"zR\"" "${frames}")
endforeach()
