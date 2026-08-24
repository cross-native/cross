# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid manual stack interfaces were accepted")
endif()

foreach(expected
        "manual stack endpoint offset is not naturally aligned"
        "overlapping manual stack endpoint range"
        "unsupported x86-64 register endpoint 'push=>discard'"
        "callee stack cleanup forbids stack output endpoints"
        "\\stack_cleanup requires \"caller\" or \"callee\"")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic: ${expected}\n${stdout}\n${stderr}")
    endif()
endforeach()
