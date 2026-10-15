# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs inlining_runtime.x on the host through its C++ driver. MODE selects
# mandatory, discretionary, or disabled inlining of the helpers.
foreach(required CC HOST_CXX SOURCE DRIVER MODE LEVEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
if(WIN32)
    set(host_abi ms_abi)
else()
    set(host_abi sysv_abi)
endif()
set(flags -${LEVEL} -fno-eval-calls -mabi=${host_abi})
if(MODE STREQUAL "discretionary")
    list(APPEND flags -DDISCRETIONARY)
elseif(MODE STREQUAL "disabled")
    list(APPEND flags -DDISABLED)
elseif(NOT MODE STREQUAL "mandatory")
    message(FATAL_ERROR "unknown MODE '${MODE}'")
endif()

execute_process(COMMAND "${CC}" ${flags} -c "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Cross compilation failed\n${out}\n${err}")
endif()
execute_process(COMMAND "${HOST_CXX}" "${DRIVER}" "${OUTPUT}.o" -o "${OUTPUT}.exe"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "driver link failed\n${out}\n${err}")
endif()
execute_process(COMMAND "${OUTPUT}.exe"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "inlining runtime check failed (${status})\n${out}\n${err}")
endif()
