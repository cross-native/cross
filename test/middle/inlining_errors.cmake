# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each case of inlining_errors.x must fail with its reason, reported once.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(expect_failure case target message)
    execute_process(
        COMMAND "${CC}" -S -O2 ${target} -D${case} "${SOURCE}"
                -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "inlining case ${case} compiled")
    endif()
    string(FIND "${stderr}" "${message}" first)
    string(FIND "${stderr}" "${message}" last REVERSE)
    if(first EQUAL -1)
        message(FATAL_ERROR
            "inlining case ${case} lacks '${message}'\n${stdout}\n${stderr}")
    endif()
    if(NOT first EQUAL last)
        message(FATAL_ERROR
            "inlining case ${case} repeats '${message}'\n${stderr}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
expect_failure(RECURSIVE "${x86}"
    "9:12: error: always_inline call to 'countdown' could not be inlined: the call is recursive")
expect_failure(MUTUAL "${x86}"
    "always_inline call to 'pong' could not be inlined: the call is recursive")
expect_failure(VARIADIC "${x86}"
    "always_inline call to 'first' could not be inlined: the callee is variadic")
expect_failure(MUSTTAIL "${x86}"
    "always_inline call to 'forward' could not be inlined: the callee contains a musttail call")
expect_failure(LOCATED "${x86}"
    "always_inline call to 'located' could not be inlined: a parameter has an explicit location")
expect_failure(MANUAL_OUTPUT "${x86}"
    "always_inline call to 'manual' could not be inlined: its output parameters belong to a manual interface")
expect_failure(NAKED_VLA -mprofile=vr4300-o32
    "raw_inline call to 'sum' could not be inlined into the naked function: a naked function cannot allocate a variable-length array")
