# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE RUNNER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -O2 -c "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR
        "aggregate ABI Cross compilation failed\n"
        "${compile_stdout}\n${compile_stderr}")
endif()

execute_process(
    COMMAND "${HOST_CXX}" "${RUNNER}" "${OUTPUT}.o" -o "${OUTPUT}.exe"
    RESULT_VARIABLE link_status
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)
if(NOT link_status EQUAL 0)
    message(FATAL_ERROR
        "aggregate ABI harness link failed\n${link_stdout}\n${link_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT}.exe"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR
        "aggregate ABI runtime probe failed (${run_status})\n"
        "${run_stdout}\n${run_stderr}")
endif()
