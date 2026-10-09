# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A tail transfer between manual interfaces that would break a physical
# contract is a precise error.

foreach(required CC ERRORS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

foreach(case IN ITEMS
        "RESULT;caller and callee result locations are incompatible"
        "STACK_RESULT;caller and callee result locations are incompatible"
        "STACK_ARGUMENT;a tail argument is passed on the stack"
        "ADDRESS;a tail argument is passed by address"
        "MIXED;only one of the caller and callee has manual ABI endpoints"
        "CLOBBER;the callee clobber contract exceeds the caller contract"
        "PRESERVED;a tail argument endpoint is preserved by the caller contract"
        "CLEANUP;caller and callee stack cleanup differ")
    list(GET case 0 name)
    list(GET case 1 pattern)
    execute_process(
        COMMAND "${CC}" -S -O0 -fno-optimize-sibling-calls
                -target x86_64-unknown-linux-gnu "-DBAD_${name}" "${ERRORS}"
                -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES
       "musttail_manual_errors[.]x:[0-9]+:[0-9]+: error: x86-64 cannot satisfy musttail: ${pattern}")
        message(FATAL_ERROR
            "manual musttail case ${name} lacks '${pattern}'\n${stdout}\n${stderr}")
    endif()
endforeach()
