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
        "overlapping hard register object 'r12'")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic: ${expected}\n${stdout}\n${stderr}")
    endif()
endforeach()

# Source validation rejects the address of a hard register object before
# lowering, so it is checked in a separate compilation.
file(WRITE "${OUTPUT}.address.x" "global void hard_address() {
    register i32 value \"r12d\" = 1;
    i32 *pointer = &value;
}
")
execute_process(
    COMMAND "${CC}" -emit-llvm "${OUTPUT}.address.x" -o "${OUTPUT}.address"
    RESULT_VARIABLE status
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR NOT stderr MATCHES "a register object has no address")
    message(FATAL_ERROR "missing diagnostic: a register object has no address\n${stderr}")
endif()
