# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid generated source unexpectedly compiled")
endif()

foreach(expected
        "expected expression"
        "in expansion of procedural macro 'inner'"
        "procedural macro 'inner' defined here"
        "in expansion of procedural macro 'outer'"
        "procedural macro 'outer' defined here")
    string(FIND "${stderr}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing generated-source origin '${expected}'\n${stdout}\n${stderr}")
    endif()
endforeach()
