# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
get_filename_component(SWITCH_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(cases errors duplicate_default outside_case bad_selector nested_label)
set(patterns "case" "default" "case|switch" "integer|scalar|switch" "case|label|switch")
list(LENGTH cases count)
math(EXPR last "${count}-1")
foreach(index RANGE ${last})
    list(GET cases ${index} name)
    list(GET patterns ${index} pattern)
    execute_process(COMMAND "${CC}" -emit-llvm -O0
        "${SWITCH_DIR}/${name}.x" -o "${OUTPUT_DIR}/${name}.ll"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${pattern}")
        message(FATAL_ERROR "invalid switch source result for ${name}\n${out}\n${err}")
    endif()
endforeach()
