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
    message(FATAL_ERROR "invalid staging expressions unexpectedly compiled")
endif()

foreach(expected
        "call to runtime-only function 'runtime_seed' cannot be evaluated during translation"
        "while evaluating call to 'enters_runtime_only'"
        "$::runtime is invalid where a translation-time value is required"
        "eval-only function 'compile_seed' cannot be called inside $::runtime"
        "$::eval cannot appear inside a $::runtime expression")
    string(FIND "${stderr}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing staging diagnostic '${expected}'\n${stdout}\n${stderr}")
    endif()
endforeach()
