# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Integer-derived static pointers are data bytes on every object format:
# no data directive of the output may name a symbol.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu x86_64-apple-darwin
               mips-unknown-elf mipsel-unknown-elf)
    set(assembly_file "${OUTPUT}-${target}.s")
    execute_process(COMMAND "${CC}" -target ${target} -O2 -S "${SOURCE}" -o "${assembly_file}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} compilation failed\n${out}\n${err}")
    endif()
    file(READ "${assembly_file}" assembly)
    if(assembly MATCHES "\n[\t ]*\\.(quad|long|word|4byte|8byte)[\t ]+[A-Za-z_.]")
        message(FATAL_ERROR "${target} relocates an integer-derived pointer\n${assembly}")
    endif()
    if(target MATCHES "^mips")
        set(word "long")
    else()
        set(word "quad")
    endif()
    if(NOT assembly MATCHES "plain:\n[\t ]*\\.${word}[\t ]+4096\n")
        message(FATAL_ERROR "${target} does not store the address 4096\n${assembly}")
    endif()
endforeach()
