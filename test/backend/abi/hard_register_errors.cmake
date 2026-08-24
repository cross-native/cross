# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid hard register objects were accepted")
endif()

foreach(expected
        "an object location requires the register storage specifier"
        "hard register 'eax' does not match object type 'i64'"
        "hard register object cannot use compiler-owned register 'rsp'"
        "overlapping hard register object 'r12'"
        "a register object has no address")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic: ${expected}\n${stdout}\n${stderr}")
    endif()
endforeach()
