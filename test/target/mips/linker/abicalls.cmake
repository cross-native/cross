# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static links of abicalls and TLS objects in o32 and n64 of both byte
# orders: $gp from _gp_disp, %neg(%gp_rel(...)), and __gnu_local_gp, the
# GOT's reserved, page, global, and initial-exec entries, JALR hints turned
# into branches, TP- and DTP-relative words, and the TLS segment, which
# GNU ld, when available, links the same; a global-dynamic and a
# local-dynamic GOT entry pair in o32; and a GOT access without _gp.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")

# The low 32 bits of VALUE (an expression) as eight hexadecimal digits.
function(word value result)
    math(EXPR number "(${value}) & 0xffffffff" OUTPUT_FORMAT HEXADECIMAL)
    string(SUBSTRING "${number}" 2 -1 digits)
    string(LENGTH "${digits}" length)
    while(length LESS 8)
        string(PREPEND digits "0")
        math(EXPR length "${length} + 1")
    endwhile()
    set(${result} "${digits}" PARENT_SCOPE)
endfunction()

function(expect_word label words index value)
    list(GET words ${index} actual)
    word("${value}" expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${label}: word ${index} is ${actual}, expected ${expected}")
    endif()
endfunction()

function(expect_value label actual expected)
    word("${actual}" actual)
    word("${expected}" expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${label} is ${actual}, expected ${expected}")
    endif()
endfunction()

# The value that the immediates of words HIGH and LOW, a %hi and %lo pair,
# give.
function(pair_value words high low result)
    list(GET words ${high} upper)
    list(GET words ${low} lower)
    math(EXPR value "((0x${upper} & 0xffff) << 16) + (((0x${lower} & 0xffff) ^ 0x8000) - 0x8000)")
    set(${result} "${value}" PARENT_SCOPE)
endfunction()

foreach(variant o32-big o32-little n64-big n64-little)
    string(REPLACE "-" ";" fields "${variant}")
    list(GET fields 0 abi)
    list(GET fields 1 order)
    set(endian -EB)
    if(order STREQUAL "little")
        set(endian -EL)
    endif()
    if(abi STREQUAL "o32")
        set(triple mips-unknown-elf)
        set(start 0x80100000)
        set(gnu_options ${endian})
        set(entry_words 1)
        # The jalr words that become branches.
        set(branches 11 15)
    else()
        set(triple mips64-unknown-elf)
        set(start 0xffffffff80100000)
        set(gnu_options ${endian} --oformat=elf64-bigmips -z max-page-size=0x10000)
        set(entry_words 2)
        set(branches 12 15)
    endif()
    if(order STREQUAL "little")
        string(REPLACE "mips" "mipsel" triple "${triple}")
        string(REPLACE "mipsel64" "mips64el" triple "${triple}")
        string(REPLACE "bigmips" "littlemips" gnu_options "${gnu_options}")
    endif()
    set(base "${OUTPUT}/${variant}")
    run_checked("${variant} source" "${AS}" -target ${triple} -march=mips3
                "${sources}/abicalls_${abi}.s" -o "${base}.o")
    run_checked("${variant} target" "${AS}" -target ${triple} -march=mips3
                "${sources}/abicalls_target_${abi}.s" -o "${base}-target.o")
    set(arguments -T "${sources}/abicalls.ld" --defsym TEXT_START=${start} "${base}.o"
                  "${base}-target.o")
    run_checked("${variant} link" "${LD}" ${arguments} -o "${base}.elf")

    section_words("${base}.elf" .text ${order} text)
    section_words("${base}.elf" .got ${order} got)
    section_words("${base}.elf" .data ${order} data)
    # The low 32 bits of the symbols, which their differences need.
    foreach(name _gp _GLOBAL_OFFSET_TABLE_ _start tls_local tls_shared)
        symbol_value("${base}.elf" ${name} value)
        string(LENGTH "${value}" length)
        if(length GREATER 8)
            math(EXPR length "${length} - 8")
            string(SUBSTRING "${value}" ${length} 8 value)
        endif()
        set(${name} "0x${value}")
    endforeach()
    math(EXPR offset "${_gp} - ${_GLOBAL_OFFSET_TABLE_}")
    if(NOT offset EQUAL 0x7ff0)
        message(FATAL_ERROR "${variant}: _gp is not 0x7ff0 past the GOT")
    endif()
    pair_value("${text}" 0 2 value)
    if(abi STREQUAL "o32")
        pair_value("${text}" 0 1 value)
    endif()
    expect_value("${variant}: the entry's $gp offset" "${value}" "${_gp} - ${_start}")
    pair_value("${text}" 3 4 value)
    expect_value("${variant}: __gnu_local_gp" "${value}" "${_gp}")
    foreach(index IN LISTS branches)
        list(GET text ${index} instruction)
        if(NOT instruction MATCHES "^0411")
            message(FATAL_ERROR "${variant}: word ${index} (${instruction}) is not a bal")
        endif()
    endforeach()
    # The reserved entries: the lazy resolver's (zero) and the module
    # pointer's, whose top bit marks it.
    expect_word("${variant} .got" "${got}" 0 0)
    set(marker 1)
    if(entry_words EQUAL 2)
        set(marker 3)
        if(order STREQUAL "big")
            set(marker 2)
        endif()
    endif()
    expect_word("${variant} .got" "${got}" ${marker} 0x80000000)
    # The initial-exec entry, which word 23 loads, holds the object's offset
    # from the thread pointer, 0x7000 past the TLS segment, whose symbols
    # hold offsets in it.
    list(GET text 23 instruction)
    math(EXPR entry "(${_gp} + (((0x${instruction} & 0xffff) ^ 0x8000) - 0x8000) - ${_GLOBAL_OFFSET_TABLE_}) / 4")
    if(order STREQUAL "big" AND entry_words EQUAL 2)
        math(EXPR entry "${entry} + 1")
    endif()
    expect_word("${variant} initial-exec entry" "${got}" ${entry} "${tls_shared} - 0x7000")
    # The low words of the TP- and DTP-relative values after the 0x10018
    # bytes of data.
    set(tprel 16390)
    set(dtprel 16391)
    if(entry_words EQUAL 2)
        set(dtprel 16392)
        if(order STREQUAL "big")
            set(tprel 16391)
            set(dtprel 16393)
        endif()
    endif()
    expect_word("${variant} TP-relative word" "${data}" ${tprel} "${tls_local} - 0x7000")
    expect_word("${variant} DTP-relative word" "${data}" ${dtprel} "${tls_shared} + 4 - 0x8000")
    run_checked("${variant} segments" "${LLVM_READELF}" -l "${base}.elf")
    expect("${variant} segments" "${run_stdout}" "\n +TLS +")

    if(GNU_LD)
        run_checked("${variant} GNU ld" "${GNU_LD}" ${gnu_options} ${arguments} -o "${base}-gnu.elf")
        same_as_gnu("${variant}" "${base}.elf" "${base}-gnu.elf")
    endif()
endforeach()

# Global-dynamic and local-dynamic entry pairs: the module 1 of a static
# executable and the offset from the DTP, 0x8000 past the TLS segment.
foreach(model gd ld)
    set(base "${OUTPUT}/${model}")
    run_checked("${model} source" "${AS}" -target mips-unknown-elf "${sources}/abicalls_${model}.s"
                -o "${base}.o")
    set(arguments -T "${sources}/abicalls.ld" --defsym TEXT_START=0x80100000 "${base}.o"
                  "${OUTPUT}/o32-big-target.o")
    run_checked("${model} link" "${LD}" ${arguments} -o "${base}.elf")
    section_words("${base}.elf" .got big got)
    section_words("${base}.elf" .text big text)
    list(GET text 0 instruction)
    symbol_value("${base}.elf" _gp gp)
    symbol_value("${base}.elf" _GLOBAL_OFFSET_TABLE_ table)
    math(EXPR entry "(0x${gp} + (((0x${instruction} & 0xffff) ^ 0x8000) - 0x8000) - 0x${table}) / 4")
    math(EXPR offset "${entry} + 1")
    expect_word("${model} module" "${got}" ${entry} 1)
    if(model STREQUAL "gd")
        symbol_value("${base}.elf" tls_shared value)
        expect_word("${model} offset" "${got}" ${offset} "0x${value} - 0x8000")
    else()
        expect_word("${model} offset" "${got}" ${offset} 0)
    endif()
    if(GNU_LD)
        run_checked("${model} GNU ld" "${GNU_LD}" -EB ${arguments} -o "${base}-gnu.elf")
        same_as_gnu("${model}" "${base}.elf" "${base}-gnu.elf")
    endif()
endforeach()

# A GOT access needs _gp.
file(WRITE "${OUTPUT}/no-gp.ld" "SECTIONS { .text 0x80100000 : { *(.text*) } .data : { *(.data) } .tdata : { *(.tdata) } .tbss : { *(.tbss) } .got : { *(.got) } }\n")
run_failing("no _gp" "${LD}" -T "${OUTPUT}/no-gp.ld" "${OUTPUT}/o32-big.o" "${OUTPUT}/o32-big-target.o"
            -o "${OUTPUT}/no-gp.elf")
expect("no _gp" "${run_stderr}"
       "o32-big[.]o:[(][.]text[.]boot[+]0x14[)]: R_MIPS_GOT16 against '[.]data' needs _gp, which nothing defines")

# TLS NOBITS sections that a script splits follow one another in the TLS
# block while the next section starts where the first one did, as LLD
# links them (GNU ld overlaps their symbols).
file(WRITE "${OUTPUT}/split-tbss.s" "\t.section .tdata,\"awT\",@progbits
\t.globl first_word
first_word:
\t.word 1
\t.section .tbss,\"awT\",@nobits
\t.globl first_block
first_block:
\t.space 16
\t.section .tbss.second,\"awT\",@nobits
\t.globl second_block
second_block:
\t.space 32
\t.text
\t.globl __start
__start:
\tlui $2, %tprel_hi(second_block)
\taddiu $2, $2, %tprel_lo(second_block)
\t.data
\t.globl after
after:
\t.word 5
")
file(WRITE "${OUTPUT}/split-tbss.ld" "SECTIONS { . = 0x80010000; .text : { *(.text) } .tdata : { *(.tdata) } .tbss : { *(.tbss) } .tbss2 : { *(.tbss.second) } .data : { *(.data) } }\n")
run_checked("split .tbss assembly" "${AS}" -target mips-unknown-elf -march=mips32r2 "${OUTPUT}/split-tbss.s"
            -o "${OUTPUT}/split-tbss.o")
run_checked("split .tbss link" "${LD}" -T "${OUTPUT}/split-tbss.ld" "${OUTPUT}/split-tbss.o"
            -o "${OUTPUT}/split-tbss.elf")
symbol_value("${OUTPUT}/split-tbss.elf" first_block first)
symbol_value("${OUTPUT}/split-tbss.elf" second_block second)
symbol_value("${OUTPUT}/split-tbss.elf" after data)
expect_value("second TLS block offset" "0x${second}" "0x${first} + 16")
if(LLVM_READELF)
    execute_process(COMMAND "${LLVM_READELF}" -lS "${OUTPUT}/split-tbss.elf" OUTPUT_VARIABLE headers)
    string(REGEX MATCH "\\.tbss +NOBITS +([0-9a-f]+)" tbss_line "${headers}")
    expect_value(".data address" "0x${data}" "0x${CMAKE_MATCH_1}")
    expect("split .tbss segment" "${headers}" "TLS +0x[0-9a-f]+ 0x[0-9a-f]+ 0x[0-9a-f]+ 0x00004 0x00034 ")
endif()
