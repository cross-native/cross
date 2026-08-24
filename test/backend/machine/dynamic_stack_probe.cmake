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
        "dynamic outgoing stack-probe compilation failed\n"
        "${compile_stdout}\n${compile_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
if(NOT assembly MATCHES "call\\.probe" OR
   NOT assembly MATCHES "leaq[\t ]+-4096\\(%rsp\\),[\t ]+%r10" OR
   NOT assembly MATCHES "testb[\t ]+\\$0,[\t ]+\\(%rsp\\)" OR
   NOT assembly MATCHES "movq[\t ]+%rsp,[\t ]+-[0-9]+\\(%rbp\\)" OR
   NOT assembly MATCHES "movq[\t ]+-[0-9]+\\(%rbp\\),[\t ]+%rsp" OR
   assembly MATCHES "__chkstk")
    message(FATAL_ERROR
        "x86-64 did not probe and restore the dynamic outgoing call frame\n"
        "${assembly}")
endif()
