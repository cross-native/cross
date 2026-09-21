# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE_DIR MODEL RUNNER OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(WIN32)
    set(host_abi ms_abi)
else()
    set(host_abi sysv_abi)
endif()
foreach(level O0 O1 O2 O3 Os Oz)
    foreach(mode ordinary noeval custom)
        set(flags "-mabi=${host_abi}" -${level})
        if(mode STREQUAL noeval)
            list(APPEND flags -fno-eval-calls -fno-inline -fno-register-allocation)
        elseif(mode STREQUAL custom)
            set(flags "--model=${MODEL}" -mabi=odd_abi -DCUSTOM_ALIAS_ABI
                      "-DHOST_ABI=\"${host_abi}\"" -${level} -fno-eval-calls -fno-inline)
        endif()
        set(stem "${OUTPUT}-${level}-${mode}")
        execute_process(COMMAND "${CC}" ${flags} -c "${SOURCE_DIR}/out_alias_valid.x" -o "${stem}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "valid alias flow rejected ${level}/${mode}\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" -DCROSS_ENTRY=out_alias_entry "${RUNNER}" "${stem}.o" -o "${stem}.exe"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "alias runtime link failed ${level}/${mode}\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "alias runtime mismatch ${level}/${mode}: ${status}")
        endif()
    endforeach()
endforeach()

foreach(target host mips mipsel mips64 mips64el)
    set(flags "-mabi=${host_abi}")
    if(target STREQUAL mips OR target STREQUAL mipsel)
        set(flags -target "${target}-unknown-elf" -march=r3000 -mabi=o32)
    elseif(target STREQUAL mips64 OR target STREQUAL mips64el)
        set(flags -target "${target}-unknown-elf" -march=mips64 -mabi=n64)
    endif()
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" ${flags} -${level} -fno-eval-calls -c
            "${SOURCE_DIR}/out_alias_valid.x" -o "${OUTPUT}-${target}-${level}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "valid alias target compile failed ${target}/${level}\n${out}\n${err}")
        endif()
        foreach(case READ COMPOUND REASSIGN BRANCH_GAP SELECT_GAP MAY_READ LOOP_GAP
                     LOOP_REASSIGN CALL_KILL COPYOUT_KILL INDIRECT_KILL UNKNOWN_WRITE
                     PARTIAL_WRITE CALL_READ PARTIAL_MEMBER MEMBER_READ DYNAMIC_READ DYNAMIC_WRITE
                     CALL_REDIRECT_READ CALL_RESULT_READ)
            execute_process(COMMAND "${CC}" ${flags} -${level} "-D${case}" -S
                "${SOURCE_DIR}/out_alias_errors.x" -o "${OUTPUT}-error.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 10)
            if(case MATCHES "READ" OR case STREQUAL COMPOUND)
                set(expected "read of 'out' parameter")
            else()
                set(expected "normal return leaves 'out' parameter")
            endif()
            if(status EQUAL 0 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "out_alias_errors\\.x:[0-9]+:[0-9]+")
                message(FATAL_ERROR "alias diagnostic ${target}/${level}/${case}\n${out}\n${err}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(LLVM_TEXT)
    find_program(LLVM_AS NAMES llvm-as)
    if(LLVM_AS)
        execute_process(COMMAND "${CC}" "-mabi=${host_abi}" -O0 -fno-eval-calls -emit-llvm
            "${SOURCE_DIR}/out_alias_valid.x" -o "${OUTPUT}.ll"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "alias LLVM serialization failed\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "alias LLVM verification failed\n${out}\n${err}")
        endif()
    else()
        message(STATUS "skipping optional alias LLVM verification: llvm-as unavailable")
    endif()
endif()
