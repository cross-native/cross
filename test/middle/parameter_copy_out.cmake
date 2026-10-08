# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs parameter_copy_out.x on the host through its C++ driver. MODE selects
# the ABI of the private functions: native (dynamic private ABIs), cross (the
# Cross ABI), or custom (the two-register odd_abi of MODEL).
foreach(required CC HOST_CXX SOURCE DRIVER MODEL MODE LEVEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
if(WIN32)
    set(host_abi ms_abi)
else()
    set(host_abi sysv_abi)
endif()
set(flags -${LEVEL} -fno-eval-calls)
if(MODE STREQUAL "native")
    list(APPEND flags "-mabi=${host_abi}")
elseif(MODE STREQUAL "cross")
    list(APPEND flags -mabi=cross -fno-private-abi "-DHOST_ABI=\"${host_abi}\"")
elseif(MODE STREQUAL "custom")
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi -fno-private-abi
                      "-DHOST_ABI=\"${host_abi}\"")
else()
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
    message(FATAL_ERROR "copy-out runtime check failed (${status})\n${out}\n${err}")
endif()

if(LLVM_TEXT)
    find_program(LLVM_AS NAMES llvm-as)
    if(LLVM_AS)
        execute_process(COMMAND "${CC}" ${flags} -emit-llvm "${SOURCE}"
                                -o "${OUTPUT}.ll"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "LLVM serialization failed\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "LLVM verification failed\n${out}\n${err}")
        endif()
    endif()
endif()
