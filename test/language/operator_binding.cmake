# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm -fno-eval-calls "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "operator binding compilation failed\n${compile_error}")
endif()
file(READ "${OUTPUT}" ir)
if(NOT ir MATCHES
   "call i32 @\"add_counts\"\\(i32 %arg0, i32 %arg1\\)")
    message(FATAL_ERROR "operator expression was not bound to its function\n${ir}")
endif()

execute_process(
    COMMAND "${CC}" -S -fno-eval-calls "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE native_status
    OUTPUT_VARIABLE native_output
    ERROR_VARIABLE native_error
)
if(NOT native_status EQUAL 0)
    message(FATAL_ERROR "native operator binding failed\n${native_error}")
endif()
file(READ "${OUTPUT}.s" assembly)
if(NOT assembly MATCHES "call[ \t]+add_counts")
    message(FATAL_ERROR
        "native backend lost the bound operator call\n${assembly}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm "${ERROR_SOURCE}" -o "${OUTPUT}.errors.ll"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_output
    ERROR_VARIABLE error_text
)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid operator bindings were accepted")
endif()
foreach(expected
        "requires at least one nominal user-defined operand type"
        "operator '=' is not overloadable with 2 operand"
        "duplicate exact operator binding for '-'"
        "operator binding redeclarations of 'inconsistent_binding' disagree")
    string(FIND "${error_text}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "missing operator diagnostic '${expected}'\n${error_text}")
    endif()
endforeach()
