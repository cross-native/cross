# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm -O2 "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "procedural body execution failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}" ir)
string(FIND "${ir}" "procedural_body_entry" entry)
string(FIND "${ir}" "ret i64 19" result)
if(entry EQUAL -1 OR result EQUAL -1)
    message(FATAL_ERROR "procedural body output was not compiled\n${ir}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm "${ERROR_SOURCE}" -o "${OUTPUT}.error.ll"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr
)
if(error_status EQUAL 0 OR
   NOT error_stderr MATCHES "did not return a token value")
    message(FATAL_ERROR "missing macro return was not diagnosed\n${error_stdout}\n${error_stderr}")
endif()
