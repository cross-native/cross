# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_vector_error source needle)
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

expect_vector_error(invalid_size
    "vector size must be a multiple of the element size")
expect_vector_error(lane_oob "fixed-vector lane index is out of range")
expect_vector_error(unsupported_width
    "supports only 128-, 256-, or 512-bit fixed-vector values")
expect_vector_error(shape_mismatch
    "vector conversion requires matching lane counts and scalability")
expect_vector_error(scalable
    "scalable-vector values are not supported by the selected target backend")
