# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" ${CC_FLAGS} -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "cc failed (${compile_result})\n${compile_stdout}\n${compile_stderr}")
endif()

execute_process(
    COMMAND lli "--entry-function=${ENTRY}" "${OUTPUT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL EXPECTED)
    message(FATAL_ERROR
        "${ENTRY} returned ${run_result}, expected ${EXPECTED}\n${run_stdout}\n${run_stderr}")
endif()
