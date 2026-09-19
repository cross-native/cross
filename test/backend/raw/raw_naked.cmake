# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(run_cc output)
    execute_process(
        COMMAND "${CC}" ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cc failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

set(linux_ir "${OUTPUT}-linux.ll")
set(windows_ir "${OUTPUT}-windows.ll")
set(linux_asm "${OUTPUT}-linux.s")
set(windows_asm "${OUTPUT}-windows.s")
set(linux_obj "${OUTPUT}-linux.o")
set(windows_obj "${OUTPUT}-windows.o")

run_cc("${linux_ir}" -emit-llvm -target x86_64-unknown-linux-gnu)
run_cc("${windows_ir}" -emit-llvm -target x86_64-w64-windows-gnu)
run_cc("${linux_asm}" -S -O0 -target x86_64-unknown-linux-gnu)
run_cc("${windows_asm}" -S -O2 -target x86_64-w64-windows-gnu)
run_cc("${linux_obj}" -c -O2 -target x86_64-unknown-linux-gnu)
run_cc("${windows_obj}" -c -O2 -target x86_64-w64-windows-gnu)

file(READ "${linux_ir}" linux_ir_text)
file(READ "${windows_ir}" windows_ir_text)
foreach(ir_text IN ITEMS linux_ir_text windows_ir_text)
    if(NOT "${${ir_text}}" MATCHES "module asm")
        message(FATAL_ERROR "raw function was not emitted as module assembly")
    endif()
    if(NOT "${${ir_text}}" MATCHES "declare .*@\"raw_const\"\(\)")
        message(FATAL_ERROR "raw function has no LLVM declaration")
    endif()
    if("${${ir_text}}" MATCHES "define [^\n]*@\"raw_const\"")
        message(FATAL_ERROR "raw function was also emitted as an LLVM definition")
    endif()
    string(REGEX MATCH "define[^}]*managed_raw_caller[^}]*}" managed_caller
                 "${${ir_text}}")
    string(FIND "${managed_caller}" "call " raw_call_position)
    string(FIND "${managed_caller}" "raw_const" raw_symbol_position)
    if(raw_call_position EQUAL -1 OR raw_symbol_position EQUAL -1)
        message(FATAL_ERROR "managed caller does not target the HIR-owned raw symbol")
    endif()
endforeach()
if(NOT linux_ir_text MATCHES "\\.type raw_const,@function")
    message(FATAL_ERROR "ELF raw assembly lacks function metadata")
endif()
if(NOT windows_ir_text MATCHES "\\.def raw_const")
    message(FATAL_ERROR "COFF raw assembly lacks function metadata")
endif()
foreach(ir_text IN ITEMS linux_ir_text windows_ir_text)
    string(FIND "${${ir_text}}" ".boot" section_position)
    if(section_position EQUAL -1)
        message(FATAL_ERROR "raw function did not retain its HIR-owned section")
    endif()
endforeach()

file(READ "${linux_asm}" linux_asm_text)
file(READ "${windows_asm}" windows_asm_text)
foreach(asm_text IN ITEMS linux_asm_text windows_asm_text)
    foreach(pattern "raw_const:" "raw_frame:" "movabsq" "pushq" "popq" "addq" "cmpq" "retq")
        if(NOT "${${asm_text}}" MATCHES "${pattern}")
            message(FATAL_ERROR "raw assembly is missing ${pattern}")
        endif()
    endforeach()
    if(NOT "${${asm_text}}" MATCHES "[.]p2align 7")
        message(FATAL_ERROR "raw function ignored aligned(128)")
    endif()
    string(REGEX MATCHALL "raw_const:" raw_labels "${${asm_text}}")
    list(LENGTH raw_labels raw_label_count)
    if(NOT raw_label_count EQUAL 1)
        message(FATAL_ERROR "raw symbol was not defined exactly once")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_error
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_error}")
endif()
foreach(instruction "$::_push" "$::_pop" "$::_ret")
    string(FIND "${registry}" "${instruction} instruction" instruction_position)
    if(instruction_position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing ${instruction}")
    endif()
endforeach()
