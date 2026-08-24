# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -fomit-frame-pointer
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "static stack-probe compilation failed\n"
        "${compile_stdout}\n${compile_stderr}")
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
        "ELF static stack-probe object emission failed\n"
        "${elf_stdout}\n${elf_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
if(NOT assembly MATCHES "frame\\.probe" OR
   NOT assembly MATCHES "leaq[\t ]+-4096\\(%rsp\\),[\t ]+%r10" OR
   NOT assembly MATCHES "testb[\t ]+\\$0,[\t ]+\\(%rsp\\)" OR
   NOT assembly MATCHES "andq[\t ]+\\$-64,[\t ]+%r11" OR
   assembly MATCHES "__chkstk")
    message(FATAL_ERROR
        "x86-64 did not inline ordinary and realigned static-frame probes\n"
        "${assembly}")
endif()
