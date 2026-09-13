# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(run_cc output)
    execute_process(
        COMMAND "${CC}" ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cc failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

set(assembly "${OUTPUT}.s")
run_cc("${assembly}" -S)
file(READ "${assembly}" text)
if(NOT text MATCHES "[.]byte 97,98,0,99,100,0")
    message(FATAL_ERROR "native byte initializer is missing\n${text}")
endif()

if(LLVM_TEXT)
    set(llvm "${OUTPUT}.ll")
    run_cc("${llvm}" -emit-llvm)
    file(READ "${llvm}" text)
    string(FIND "${text}"
           "constant [6 x i8] [i8 97, i8 98, i8 0, i8 99, i8 100, i8 0]"
           position)
    if(position EQUAL -1)
        message(FATAL_ERROR "LLVM byte initializer is missing\n${text}")
    endif()
    find_program(LLVM_AS NAMES llvm-as)
    if(LLVM_AS)
        execute_process(COMMAND "${LLVM_AS}" "${llvm}" -o "${OUTPUT}.bc"
                        RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                        ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "llvm-as failed\n${stdout}\n${stderr}")
        endif()
    endif()
endif()

if(GIMPLE_TEXT)
    set(gimple "${OUTPUT}.gimple.c")
    run_cc("${gimple}" -emit-gimple)
    file(READ "${gimple}" text)
    string(FIND "${text}" "= {97, 98, 0, 99, 100, 0};" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "GIMPLE byte initializer is missing\n${text}")
    endif()
    find_program(GCC NAMES gcc)
    if(GCC)
        execute_process(COMMAND "${GCC}" -fgimple -c "${gimple}"
                                -o "${OUTPUT}.gimple.o"
                        RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                        ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "GCC rejected GIMPLE byte initializer\n${stdout}\n${stderr}")
        endif()
    endif()
endif()
