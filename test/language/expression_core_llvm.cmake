# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -emit-llvm -O0 "${SOURCE}" -o "${OUTPUT}"
                RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "LLVM expression emission failed\n${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" text)
foreach(opcode "ptrtoint ptr" "inttoptr i")
    string(FIND "${text}" "${opcode}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "LLVM expression output is missing ${opcode}")
    endif()
endforeach()

find_program(LLVM_AS NAMES llvm-as)
if(LLVM_AS)
    execute_process(COMMAND "${LLVM_AS}" "${OUTPUT}" -o "${OUTPUT}.bc"
                    RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "llvm-as rejected expression output\n${stdout}\n${stderr}")
    endif()
endif()
