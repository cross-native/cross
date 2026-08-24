# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC LLVM_AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm -O2 "${SOURCE}" -o "${OUTPUT}.ll"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR
        "cc failed (${compile_status})\n${compile_stdout}\n${compile_stderr}")
endif()

execute_process(
    COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
    RESULT_VARIABLE assemble_status
    OUTPUT_VARIABLE assemble_stdout
    ERROR_VARIABLE assemble_stderr
)
if(NOT assemble_status EQUAL 0)
    message(FATAL_ERROR
        "llvm-as failed (${assemble_status})\n${assemble_stdout}\n${assemble_stderr}")
endif()
