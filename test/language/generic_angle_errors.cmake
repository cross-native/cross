# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(cases generic_angle_conflict generic_angle_missing_value
          generic_angle_mixed_declaration generic_angle_assertion)
set(patterns "conflicting deductions" "requires an explicit argument"
             "cannot be combined" "static_assert failed")
list(LENGTH cases count)
math(EXPR last "${count}-1")
foreach(index RANGE ${last})
    list(GET cases ${index} name)
    list(GET patterns ${index} pattern)
    execute_process(
        COMMAND "${CC}" -S -O0 "${SOURCE_DIR}/${name}.x"
                -o "${OUTPUT_DIR}/${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(status EQUAL 0 OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "${name}: expected '${pattern}' failure\n${output}\n${error}")
    endif()
endforeach()
