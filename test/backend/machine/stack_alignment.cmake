# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -fomit-frame-pointer -mno-red-zone
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "over-aligned frame compilation failed\n"
        "${compile_stdout}\n${compile_stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
if(NOT assembly MATCHES "pushq[\t ]+%rbp" OR
   NOT assembly MATCHES "andq[\t ]+\\$-64,[\t ]+%rsp" OR
   NOT assembly MATCHES "movq[\t ]+%rbp,[\t ]+%rsp" OR
   NOT assembly MATCHES "16\\(%rbp\\)")
    message(FATAL_ERROR
        "x86-64 did not create and restore a 64-byte-aligned frame\n"
        "${assembly}")
endif()
