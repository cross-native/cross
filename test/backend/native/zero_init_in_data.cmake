# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -fzero-init-in-data places zero-valued static objects with the initialized
# data, with and without -fdata-sections, on every object format.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(WRITE "${OUTPUT}.x"
    "global i32 zero_global;\n"
    "global i32 zero_explicit = 0;\n"
    "[[noinit]] global u32 noinit_value;\n"
    "global i32 read_all() { return zero_global + zero_explicit + (i32)noinit_value; }\n")
file(WRITE "${OUTPUT}-tls.x"
    "[[thread_local]] global u32 zero_tls;\n"
    "global u32 read_tls() { return zero_tls; }\n")

function(compile output_var source target suffix)
    execute_process(COMMAND "${CC}" -S -target "${target}" ${ARGN} "${source}"
        -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${suffix} failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" text)
    set(${output_var} "${text}" PARENT_SCOPE)
endfunction()

function(expect text suffix)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${suffix} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

function(reject text suffix)
    foreach(pattern IN LISTS ARGN)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "${suffix} has '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# The label of each object follows the directive of its section.
foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
    compile(text "${OUTPUT}.x" ${target} ${target}-default)
    expect("${text}" ${target}-default "\n[.]bss\n[^:]*zero_global:" "\n[.]data\n[^:]*zero_explicit:"
        "[.]noinit\"?,\"aw\",@nobits\n[^:]*noinit_value:")
    compile(text "${OUTPUT}.x" ${target} ${target}-data -fzero-init-in-data)
    expect("${text}" ${target}-data "\n[.]data\n[^:]*zero_global:" "\n[.]data\n[^:]*zero_explicit:"
        "[.]noinit\"?,\"aw\",@nobits\n[^:]*noinit_value:")
    reject("${text}" ${target}-data "[.]bss")
    compile(text "${OUTPUT}.x" ${target} ${target}-split -fdata-sections)
    expect("${text}" ${target}-split "\"[.]bss[.]zero_global\",\"aw\",@nobits")
    compile(text "${OUTPUT}.x" ${target} ${target}-split-data -fdata-sections -fzero-init-in-data)
    expect("${text}" ${target}-split-data "\"[.]data[.]zero_global\",\"aw\",@progbits"
        "\"[.]noinit[.]noinit_value\",\"aw\",@nobits")
    reject("${text}" ${target}-split-data "[.]bss")
endforeach()

compile(text "${OUTPUT}-tls.x" x86_64-unknown-linux-gnu tls-default)
expect("${text}" tls-default "\"[.]tbss\",\"awT\",@nobits\n[^:]*zero_tls:")
compile(text "${OUTPUT}-tls.x" x86_64-unknown-linux-gnu tls-data -fzero-init-in-data)
expect("${text}" tls-data "\"[.]tdata\",\"awT\",@progbits\n[^:]*zero_tls:")

set(coff x86_64-w64-windows-gnu)
compile(text "${OUTPUT}.x" ${coff} coff-default)
expect("${text}" coff-default "\n[.]bss\n[^:]*zero_global:")
compile(text "${OUTPUT}.x" ${coff} coff-data -fzero-init-in-data)
expect("${text}" coff-data "\n[.]data\n[^:]*zero_global:")
reject("${text}" coff-data "[.]bss")
compile(text "${OUTPUT}.x" ${coff} coff-split -fdata-sections)
expect("${text}" coff-split "\"[.]bss[$]zero_global\",\"bw\"")
compile(text "${OUTPUT}.x" ${coff} coff-split-data -fdata-sections -fzero-init-in-data)
expect("${text}" coff-split-data "\"[.]data[$]zero_global\",\"dw\"")
reject("${text}" coff-split-data "[.]bss")

set(macho x86_64-apple-darwin)
compile(text "${OUTPUT}.x" ${macho} macho-default)
expect("${text}" macho-default "__DATA,__bss,zerofill\n[^:]*_zero_global:")
compile(text "${OUTPUT}.x" ${macho} macho-data -fzero-init-in-data)
expect("${text}" macho-data "__DATA,__data\n[^:]*_zero_global:")
reject("${text}" macho-data "__bss")
