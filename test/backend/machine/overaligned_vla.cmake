# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "over-aligned VLA compilation failed\n"
        "${compile_stdout}\n${compile_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
if(NOT assembly MATCHES "pushq[\t ]+%r1[2-5]" OR
   NOT assembly MATCHES "andq[\t ]+\\$-64,[\t ]+%rsp" OR
   NOT assembly MATCHES "movq[\t ]+%rsp,[\t ]+%rbp" OR
   NOT assembly MATCHES "call[\t ]+[^\r\n]*aligned_vla_callee" OR
   NOT assembly MATCHES "movq[\t ]+%r1[2-5],[\t ]+%rsp" OR
   NOT assembly MATCHES "popq[\t ]+%r1[2-5]")
    message(FATAL_ERROR
        "x86-64 did not retain an aligned fixed-frame base and VLA anchor\n"
        "${assembly}")
endif()

execute_process(
    COMMAND "${CC}" -c -O2 -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.elf.o"
    RESULT_VARIABLE elf_status
    OUTPUT_VARIABLE elf_stdout
    ERROR_VARIABLE elf_stderr
)
if(NOT elf_status EQUAL 0)
    message(FATAL_ERROR
        "ELF over-aligned VLA object emission failed\n"
        "${elf_stdout}\n${elf_stderr}")
endif()
