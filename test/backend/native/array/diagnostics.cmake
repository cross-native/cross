# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_array_error source needle)
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

expect_array_error(zero_bound
    "fixed array bound must be a positive integer translation-time value")
expect_array_error(omitted_bound
    "an omitted array bound requires a u8 string initializer")
expect_array_error(initializer
    "aggregate array initializers are not implemented yet")
expect_array_error(vla_inner_bound
    "only the outermost array bound may be a runtime value")
expect_array_error(vla_wide_bound
    "variable-length array bound cannot exceed pointer width")
expect_array_error(vla_goto
    "goto in a function with variable-length array storage is not implemented yet")
