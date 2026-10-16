# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(expect_failure case pattern)
    execute_process(
        COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
                "-DRAW_STATIC_ERROR=${case}" ${ARGN} "${SOURCE}"
                -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 1 OR NOT stderr MATCHES "${pattern}" OR
       NOT stderr MATCHES "raw_static_object_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR
            "case ${case} ${ARGN}: expected '${pattern}', exit ${status}\n"
            "${stdout}\n${stderr}")
    endif()
endfunction()

expect_failure(0 "raw code cannot address thread-local object 'per_thread'")
expect_failure(1 "raw code cannot address preemptible object 'exported' without a GOT entry" -fpic)
expect_failure(1 "needs the small or kernel code model" -mcmodel=medium)
expect_failure(2 "raw-compatible code can take only the address of a static-duration object")
expect_failure(3 "raw code reaches fixed-address object 'far_status' only through a 32-bit absolute address")

# Position-independent executables reach their own definitions directly.
execute_process(
    COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu -DRAW_STATIC_ERROR=1
            -fpie "${SOURCE}" -o "${OUTPUT}-pie.s"
    RESULT_VARIABLE status
    ERROR_VARIABLE stderr
)
file(READ "${OUTPUT}-pie.s" pie)
if(NOT status EQUAL 0 OR NOT pie MATCHES "movl[\t ]+exported[(]%rip[)], %eax")
    message(FATAL_ERROR "PIE raw object access is not RIP-relative\n${stderr}\n${pie}")
endif()

# A fixed address in range is an absolute memory operand.
execute_process(
    COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu -DRAW_STATIC_ERROR=4
            "${SOURCE}" -o "${OUTPUT}-fixed.s"
    RESULT_VARIABLE status
    ERROR_VARIABLE stderr
)
file(READ "${OUTPUT}-fixed.s" fixed)
if(NOT status EQUAL 0 OR NOT fixed MATCHES "movl[\t ]+268435456, %eax")
    message(FATAL_ERROR "fixed-address raw object access is not absolute\n${stderr}\n${fixed}")
endif()
