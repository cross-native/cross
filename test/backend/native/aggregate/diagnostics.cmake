# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_aggregate_error source needle)
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

expect_aggregate_error(incomplete "object has incomplete type")
expect_aggregate_error(duplicate_member "duplicate record member 'value'")
expect_aggregate_error(unknown_member "has no member named 'y'")
expect_aggregate_error(bit_field "record bit-fields are not implemented yet")
expect_aggregate_error(initializer "a record value requires the same nominal record type")
expect_aggregate_error(recursive_value "contains itself by value")

execute_process(
    COMMAND "${CC}" -S "${SOURCE_DIR}/packed_address.x"
            -o "${OUTPUT}-packed-address.s"
    RESULT_VARIABLE packed_status
    OUTPUT_VARIABLE packed_stdout
    ERROR_VARIABLE packed_stderr
)
if(NOT packed_status EQUAL 0)
    message(FATAL_ERROR
        "packed_address.x failed to compile\n${packed_stdout}\n${packed_stderr}")
endif()
string(FIND "${packed_stderr}"
    "forming a pointer to a potentially under-aligned packed member"
    packed_position)
if(packed_position EQUAL -1)
    message(FATAL_ERROR
        "packed_address.x did not report its alignment warning\n${packed_stderr}")
endif()
