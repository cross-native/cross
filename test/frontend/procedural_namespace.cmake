# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
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
    message(FATAL_ERROR "namespace-aware procedural macros failed\n${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" ir)
foreach(expected
        "global i64 66"
        "global i64 77"
        "app::generated_entry")
    string(FIND "${ir}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "namespace macro output is missing '${expected}'\n${ir}")
    endif()
endforeach()
if(ir MATCHES "\\[\\[macro\\]\\]|identity!")
    message(FATAL_ERROR "procedural syntax survived expansion\n${ir}")
endif()
