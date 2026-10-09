# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each case compiles one section of the source and expects one source-located
# diagnostic.
function(expect_error target case expected)
    execute_process(
        COMMAND "${CC}" -target ${target} -D${case} -S "${SOURCE}"
                -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "${case} was accepted")
    endif()
    if(NOT stderr MATCHES "manual_indirect_errors.x:[0-9]+:[0-9]+: error: ${expected}")
        message(FATAL_ERROR "${case}: missing diagnostic '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

if(ARCH STREQUAL mips)
    expect_error(mips-unknown-elf MIPS_ENDPOINTS
        "manual MIPS function-pointer endpoints are not implemented yet")
    expect_error(mips-unknown-elf MIPS_ADAPTER
        "manual MIPS result endpoints are not implemented yet")
else()
    expect_error(x86_64-unknown-linux-gnu VALUE_CONVERSION
        "a function-pointer value cannot change its callable parameter endpoints, result location, clobbers, and stack cleanup in return")
    expect_error(x86_64-unknown-linux-gnu CONDITIONAL_ARMS
        "conditional function-pointer operands differ in their callable parameter endpoints and result location")
    expect_error(x86_64-unknown-linux-gnu CONDITIONAL_VALUE
        "a function-pointer value cannot change its callable parameter endpoints and result location in return")
    expect_error(x86_64-unknown-linux-gnu VARIADIC_CALL
        "calls to a variadic interface with manual x86-64 locations are not implemented yet")
    expect_error(x86_64-unknown-linux-gnu RESERVED_RESULT
        "indirect result cannot use compiler-owned register 'rsp'")
    expect_error(x86_64-unknown-linux-gnu CLEANUP_OUTPUT
        "callee stack cleanup forbids stack output endpoints")
    expect_error(x86_64-unknown-linux-gnu STACK_POINTER_ENDPOINT
        "a managed call cannot pass a stack or frame pointer endpoint")
endif()
