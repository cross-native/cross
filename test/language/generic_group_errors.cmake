# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

execute_process(
    COMMAND "${CC}" -S
            "${SOURCE_DIR}/generic_group_mismatch_use.x"
            "${SOURCE_DIR}/generic_group_mismatch_def.x"
            -o "${OUTPUT_DIR}/mismatch.s"
    RESULT_VARIABLE mismatch_status
    ERROR_VARIABLE mismatch_error)
if(mismatch_status EQUAL 0 OR
   NOT mismatch_error MATCHES "incompatible interfaces")
    message(FATAL_ERROR "expected incompatible generic declaration\n${mismatch_error}")
endif()

execute_process(
    COMMAND "${CC}" -S "${SOURCE_DIR}/generic_group_undefined.x"
            -o "${OUTPUT_DIR}/undefined.s"
    RESULT_VARIABLE undefined_status
    ERROR_VARIABLE undefined_error)
if(undefined_status EQUAL 0 OR
   NOT undefined_error MATCHES "definition of generic function.*not visible")
    message(FATAL_ERROR "expected missing generic definition\n${undefined_error}")
endif()

foreach(kind explicit inferred)
    if(kind STREQUAL "explicit")
        set(source "${SOURCE_DIR}/generic_group_undeclared_use.x")
    else()
        set(source "${SOURCE_DIR}/generic_group_undeclared_inferred_use.x")
    endif()
    execute_process(
        COMMAND "${CC}" -S "${source}"
                "${SOURCE_DIR}/generic_group_undeclared_def.x"
                -o "${OUTPUT_DIR}/undeclared-${kind}.s"
        RESULT_VARIABLE undeclared_status
        ERROR_VARIABLE undeclared_error)
    if(undeclared_status EQUAL 0 OR
       NOT undeclared_error MATCHES "must be declared before use")
        message(FATAL_ERROR "expected declaration-before-use error for ${kind}\n${undeclared_error}")
    endif()
endforeach()
