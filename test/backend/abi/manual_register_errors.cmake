# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid manual-register interfaces were accepted")
endif()

foreach(expected
        "manual output register 'rbx' is preserved"
        "register endpoint 'eax' does not match parameter type 'i64'"
        "overlapping manual input register 'rax'"
        "indirect endpoint requires a 64-bit integer address register"
        "manual endpoint cannot use compiler-owned register 'rsp'"
        "register endpoint 'xmm0' does not match parameter type 'i16'")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic: ${expected}\n${stdout}\n${stderr}")
    endif()
endforeach()
