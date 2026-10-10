# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Compiles SOURCE as two groups, under the trapping-fp profile with TRAPPING
# and under the default profile, links them with the host runner, and runs
# test_entry, which returns zero when every operation raised the expected
# MXCSR flags.

foreach(required CC HOST_CXX SOURCE MODEL RUNNER OUTPUT LEVEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

# The harness is C++, so its entry uses the host's C ABI.
if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()

set(objects "")
foreach(group trapping masked)
    set(flags "")
    if(group STREQUAL "trapping")
        set(flags "--model=${MODEL}" -mprofile=trapping-fp -DTRAPPING)
    endif()
    execute_process(
        COMMAND "${CC}" "-mabi=${HOST_ABI}" -${LEVEL} ${flags}
                -c "${SOURCE}" -o "${OUTPUT}.${group}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${group} compilation failed\n${stdout}\n${stderr}")
    endif()
    list(APPEND objects "${OUTPUT}.${group}.o")
endforeach()

execute_process(
    COMMAND "${HOST_CXX}" -DCROSS_ENTRY=test_entry "${RUNNER}" ${objects}
            -o "${OUTPUT}.exe"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "link failed\n${stdout}\n${stderr}")
endif()
execute_process(COMMAND "${OUTPUT}.exe"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "test_entry returned ${status}\n${stdout}\n${stderr}")
endif()
