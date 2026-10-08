# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${out}\n${err}")
    endif()
endfunction()

find_program(LLVM_AS NAMES llvm-as)
find_program(LLC NAMES llc)
if(LLVM_AS AND LLC)
    if(WIN32)
        set(host_abi ms_abi)
    else()
        set(host_abi sysv_abi)
    endif()
    checked("LLVM serialization" "${CC}" -emit-llvm -O2 -mabi=${host_abi}
        "${SOURCE}" -o "${OUTPUT}/patch.ll")
    checked("LLVM assembly" "${LLVM_AS}" "${OUTPUT}/patch.ll" -o "${OUTPUT}/patch.bc")
    checked("LLVM object" "${LLC}" -filetype=obj "${OUTPUT}/patch.bc" -o "${OUTPUT}/patch.o")
    checked("LLVM host harness" "${HOST_CXX}" "${CMAKE_CURRENT_LIST_DIR}/syntax_patch_identity_runner.cpp"
        "${OUTPUT}/patch.o" -o "${OUTPUT}/patch.exe")
    execute_process(COMMAND "${OUTPUT}/patch.exe" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 61)
        message(FATAL_ERROR "LLVM rewritten patch result ${status}, expected 61\n${out}\n${err}")
    endif()
else()
    message(STATUS "skipping optional LLVM patch execution: llvm-as and llc are required")
endif()
