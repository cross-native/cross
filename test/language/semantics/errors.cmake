# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(cases bad_shift bad_overflow bad_effects bad_write bad_uninitialized bad_division bad_hidden_write bad_atomic_input bad_register_input bad_out_const bad_inout_const)
set(expect "shift" "overflow" "volatile" "cannot write a const cell" "uninitialized" "division by zero" "cannot write a const cell" "cannot write a const cell" "cannot write a const cell" "parameter cells cannot be const" "parameter cells cannot be const")
list(LENGTH cases n)
math(EXPR last "${n}-1")
foreach(i RANGE ${last})
    list(GET cases ${i} name)
    list(GET expect ${i} pattern)
    configure_file("${SOURCE_DIR}/${name}.x" "${OUTPUT_DIR}/${name}.x" COPYONLY)
    execute_process(COMMAND "${CC}" -emit-llvm -O2 "${OUTPUT_DIR}/${name}.x"
        -o "${OUTPUT_DIR}/${name}.ll" RESULT_VARIABLE status
        OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0)
        message(FATAL_ERROR "invalid semantic source accepted: ${name}")
    endif()
    if(NOT err MATCHES "${pattern}")
        message(FATAL_ERROR "diagnostic for ${name} lacks '${pattern}'\n${err}")
    endif()
endforeach()
configure_file("${SOURCE_DIR}/qualifier_errors.x" "${OUTPUT_DIR}/qualifier_errors.x" COPYONLY)
execute_process(COMMAND "${CC}" -emit-llvm -O2 "${OUTPUT_DIR}/qualifier_errors.x"
    -o "${OUTPUT_DIR}/qualifier_errors.ll" RESULT_VARIABLE status
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "qualifier")
    message(FATAL_ERROR "qualifier-dropping conversion was accepted\n${err}")
endif()
file(GLOB stale "${SOURCE_DIR}/*.ll")
if(stale)
    message(FATAL_ERROR "diagnostic test found stale generated files in source tree: ${stale}")
endif()
