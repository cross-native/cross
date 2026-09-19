# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE RUNNER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()

foreach(level O0 O2)
    execute_process(
        COMMAND "${CC}" "-mabi=${HOST_ABI}" -${level} -c
                "${SOURCE}" -o "${OUTPUT}-${level}.o"
        RESULT_VARIABLE compile_status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    if(NOT compile_status EQUAL 0)
        message(FATAL_ERROR
            "${level} VLA bounds compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    foreach(entry vla_bounds_designator vla_bounds_string)
        execute_process(
            COMMAND "${HOST_CXX}" "-DCROSS_ENTRY=${entry}"
                    "${RUNNER}" "${OUTPUT}-${level}.o"
                    -o "${OUTPUT}-${level}-${entry}.exe"
            RESULT_VARIABLE link_status
            OUTPUT_VARIABLE link_stdout
            ERROR_VARIABLE link_stderr)
        if(NOT link_status EQUAL 0)
            message(FATAL_ERROR
                "${level} ${entry} link failed\n"
                "${link_stdout}\n${link_stderr}")
        endif()
        execute_process(
            COMMAND "${OUTPUT}-${level}-${entry}.exe"
            RESULT_VARIABLE run_status
            OUTPUT_VARIABLE run_stdout
            ERROR_VARIABLE run_stderr)
        if(run_status EQUAL 0 OR
           "${run_status}" MATCHES "Stack overflow")
            message(FATAL_ERROR
                "${level} ${entry} did not trap on too-small VLA extent: "
                "${run_status}\n${run_stdout}\n${run_stderr}")
        endif()
    endforeach()
endforeach()
