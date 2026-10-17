# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The integrated x86-64 assembler produces, for a given AT&T-syntax input and
# object format, an object semantically identical to llvm-mc's: same section
# contents, relocations, and symbols. SOURCE names the .s file and TRIPLE the
# target.

foreach(required AS SOURCE TRIPLE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_READOBJ NAMES llvm-readobj)
find_program(LLVM_OBJDUMP NAMES llvm-objdump)
find_program(PYTHON NAMES python3 python)
if(NOT LLVM_MC OR NOT LLVM_READOBJ OR NOT LLVM_OBJDUMP OR NOT PYTHON)
    message(STATUS "skipping: llvm-mc, llvm-readobj, llvm-objdump, and python are required")
    return()
endif()

get_filename_component(here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT}")
set(cross_obj "${OUTPUT}/cross.o")
set(ref_obj "${OUTPUT}/ref.o")

execute_process(COMMAND "${AS}" -target "${TRIPLE}" "${SOURCE}" -o "${cross_obj}"
                RESULT_VARIABLE cross_rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT cross_rc EQUAL 0)
    message(FATAL_ERROR "as failed:\n${out}${err}")
endif()
execute_process(COMMAND "${LLVM_MC}" --filetype=obj "--triple=${TRIPLE}" "${SOURCE}" -o "${ref_obj}"
                RESULT_VARIABLE ref_rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT ref_rc EQUAL 0)
    message(FATAL_ERROR "llvm-mc failed:\n${out}${err}")
endif()
execute_process(COMMAND "${PYTHON}" "${here}/compare.py" "${LLVM_READOBJ}" "${LLVM_OBJDUMP}"
                        "${cross_obj}" "${ref_obj}"
                RESULT_VARIABLE diff_rc OUTPUT_VARIABLE diff_out)
if(NOT diff_rc EQUAL 0)
    message(FATAL_ERROR "object differs from llvm-mc for ${SOURCE} (${TRIPLE}):\n${diff_out}")
endif()
message(STATUS "${SOURCE} matches llvm-mc for ${TRIPLE}")
