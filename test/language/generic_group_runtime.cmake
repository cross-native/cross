# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE_USE SOURCE_DEF RUNNER OUTPUT ENTRY LEVEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()
set(flags "-mabi=${HOST_ABI}" "-${LEVEL}")
if(DEFINED MODEL AND NOT "${MODEL}" STREQUAL "")
    list(APPEND flags "--model=${MODEL}")
endif()

foreach(order use_first def_first)
    if(order STREQUAL "use_first")
        set(inputs "${SOURCE_USE}" "${SOURCE_DEF}")
    else()
        set(inputs "${SOURCE_DEF}" "${SOURCE_USE}")
    endif()
    execute_process(
        COMMAND "${CC}" ${flags} -c ${inputs}
                -o "${OUTPUT}-${order}.o"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR "${order}: Cross compilation failed\n${compile_stdout}\n${compile_stderr}")
    endif()
    execute_process(
        COMMAND "${HOST_CXX}" "-DCROSS_ENTRY=${ENTRY}"
                "${RUNNER}" "${OUTPUT}-${order}.o"
                -o "${OUTPUT}-${order}.exe"
        RESULT_VARIABLE link_result
        OUTPUT_VARIABLE link_stdout
        ERROR_VARIABLE link_stderr)
    if(NOT link_result EQUAL 0)
        message(FATAL_ERROR "${order}: link failed\n${link_stdout}\n${link_stderr}")
    endif()
    execute_process(
        COMMAND "${OUTPUT}-${order}.exe"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr)
    if(NOT run_result EQUAL 1)
        message(FATAL_ERROR "${order}: expected exit 1, got ${run_result}\n${run_stdout}\n${run_stderr}")
    endif()
endforeach()
