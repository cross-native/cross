# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE RUNNER OUTPUT ENTRY EXPECTED)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

# The harness is C++, so this boundary intentionally opts out of the default
# Cross ABI. Keep CC_FLAGS last so an ABI-specific test can override it.
if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()

execute_process(
    COMMAND "${CC}" "-mabi=${HOST_ABI}" ${CC_FLAGS}
            -c "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "native Cross compilation failed (${compile_result})\n"
        "${compile_stdout}\n${compile_stderr}")
endif()

# EXTRA_SOURCES are further compilation groups linked into the same program.
set(objects "${OUTPUT}.o")
set(group 0)
foreach(extra IN LISTS EXTRA_SOURCES)
    math(EXPR group "${group} + 1")
    execute_process(
        COMMAND "${CC}" "-mabi=${HOST_ABI}" ${CC_FLAGS}
                -c "${extra}" -o "${OUTPUT}.${group}.o"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "native Cross compilation of ${extra} failed (${compile_result})\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    list(APPEND objects "${OUTPUT}.${group}.o")
endforeach()

execute_process(
    COMMAND "${HOST_CXX}" "-DCROSS_ENTRY=${ENTRY}" "${RUNNER}"
            ${objects} -o "${OUTPUT}.exe"
    RESULT_VARIABLE link_result
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR
        "native runtime harness link failed (${link_result})\n"
        "${link_stdout}\n${link_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT}.exe"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL EXPECTED)
    message(FATAL_ERROR
        "${ENTRY} returned ${run_result}, expected ${EXPECTED}\n"
        "${run_stdout}\n${run_stderr}")
endif()
