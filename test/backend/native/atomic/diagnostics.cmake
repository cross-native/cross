# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_atomic_error source needle)
    execute_process(
        COMMAND "${CC}" -S "${SOURCE_DIR}/${source}.x"
                -o "${OUTPUT}-${source}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0)
        message(FATAL_ERROR "${source}.x unexpectedly compiled")
    endif()
    string(FIND "${stderr}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "${source}.x did not report '${needle}'\n${stdout}\n${stderr}")
    endif()
endfunction()

expect_atomic_error(invalid_load_order
    "atomic load order must be relaxed, acquire, or seq_cst")
expect_atomic_error(invalid_store_order
    "atomic store order must be relaxed, release, or seq_cst")
expect_atomic_error(invalid_failure_order
    "failure order is invalid or stronger than success")
expect_atomic_error(non_atomic_pointer
    "requires a pointer to an atomic-qualified scalar")
expect_atomic_error(unsupported_width
    "has no runtime-free atomic operation for u128")
expect_atomic_error(invalid_type
    "atomic qualifier requires an integer, floating, or pointer object type")
expect_atomic_error(hard_register
    "an atomic object cannot be bound to a machine register")
