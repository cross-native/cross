# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Serializes SOURCE as GIMPLE at each of LEVELS (default O0 and O2), compiles
# it with GCC, links the C HARNESS, and requires the harness to exit with
# EXPECTED. The GIMPLE text must match PATTERN when one is given.
foreach(required CC GCC SOURCE HARNESS OUTPUT EXPECTED)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT DEFINED LEVELS)
    set(LEVELS O0 O2)
endif()
foreach(level IN LISTS LEVELS)
    set(gimple_source "${OUTPUT}.${level}.gimple.c")
    set(executable "${OUTPUT}.${level}.exe")
    execute_process(
        COMMAND "${CC}" -emit-gimple -${level} "${SOURCE}" -o "${gimple_source}"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Cross GIMPLE serialization failed:\n${err}")
    endif()
    if(DEFINED PATTERN)
        file(READ "${gimple_source}" text)
        if(NOT text MATCHES "${PATTERN}")
            message(FATAL_ERROR "-${level} GIMPLE lacks '${PATTERN}'")
        endif()
    endif()
    execute_process(
        COMMAND "${GCC}" -c -${level} -fgimple -ffreestanding -fno-builtin
                "${gimple_source}" -o "${OUTPUT}.${level}.o"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GCC rejected Cross GIMPLE:\n${err}")
    endif()
    execute_process(
        COMMAND "${GCC}" -O2 "${HARNESS}" "${OUTPUT}.${level}.o" -o "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GCC could not link the harness:\n${err}")
    endif()
    execute_process(COMMAND "${executable}" RESULT_VARIABLE status)
    if(NOT status EQUAL EXPECTED)
        message(FATAL_ERROR "-${level} harness returned ${status}, expected ${EXPECTED}")
    endif()
endforeach()
