# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid mixed manual-register interfaces were accepted")
endif()

foreach(expected
        "overlapping manual input register 'r10'"
        "ordinary result overlaps manual output register 'rax'")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic: ${expected}\n${stdout}\n${stderr}")
    endif()
endforeach()
