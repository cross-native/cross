# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(compile_assembly output)
    execute_process(
        COMMAND "${CC}" -S ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "early-exit compilation failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

function(extract_function source_assembly symbol variable)
    string(FIND "${source_assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "could not find assembly body for ${symbol}")
    endif()
    string(SUBSTRING "${source_assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    if(length EQUAL -1)
        string(FIND "${tail}" ".seh_endproc" length)
    endif()
    if(length EQUAL -1)
        message(FATAL_ERROR "could not find assembly end for ${symbol}")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

compile_assembly("${OUTPUT}" -O3 -march=x86-64-v3)
file(READ "${OUTPUT}" optimized)
extract_function("${optimized}" early_exit_less less_body)
foreach(opcode vpxor vpcmpgtq vptest)
    if(NOT less_body MATCHES "[	 ]${opcode}[	 ]")
        message(FATAL_ERROR
            "early-exit loop did not select ${opcode}\n${less_body}")
    endif()
endforeach()
if(NOT less_body MATCHES "vpxor[\t ]+\\(")
    message(FATAL_ERROR
        "early-exit loop did not fold its vector load into VPXOR\n${less_body}")
endif()
if(NOT less_body MATCHES
   "addq[\t ]+\\$4,[^\n]*\n[\t ]+vpcmpgtq")
    message(FATAL_ERROR
        "early-exit loop did not advance its induction register before the "
        "vector test\n${less_body}")
endif()
if(NOT less_body MATCHES
   "vptest[^\n]*\n[\t ]+jb[\t ]+\\.Lcross\\.machine")
    message(FATAL_ERROR
        "early-exit loop did not use a direct conditional backedge\n"
        "${less_body}")
endif()
if(NOT less_body MATCHES "subq[\t ]+\\$4,")
    message(FATAL_ERROR
        "early-exit loop did not restore its speculative advance on exit\n"
        "${less_body}")
endif()
if(less_body MATCHES "leaq[\t ]+4\\(")
    message(FATAL_ERROR
        "early-exit loop retained a separate induction result\n${less_body}")
endif()
if(less_body MATCHES
   "vmovdqu[	 ]+[^\n]*%rsp|vmovdqu[	 ]+%ymm[0-9]+,[	 ]+[^\n]*%rsp")
    message(FATAL_ERROR
        "fused early-exit vector comparison retained a stack spill\n${less_body}")
endif()

extract_function("${optimized}" early_exit_not_equal not_equal_body)
extract_function("${optimized}" early_exit_reversed reversed_body)
extract_function("${optimized}" early_exit_signed signed_body)
foreach(body not_equal_body reversed_body signed_body)
    if(NOT ${body} MATCHES "[	 ]vptest[	 ]")
        message(FATAL_ERROR
            "${body} did not use vector early-exit lowering\n${${body}}")
    endif()
endforeach()
extract_function("${optimized}" early_exit_volatile volatile_body)
if(volatile_body MATCHES "[	 ]vptest[	 ]")
    message(FATAL_ERROR
        "volatile early-exit load was illegally vectorized\n${volatile_body}")
endif()

compile_assembly("${OUTPUT}.disabled.s" -O3 -march=x86-64-v3
                 -fno-tree-early-exit-vectorize)
file(READ "${OUTPUT}.disabled.s" disabled)
extract_function("${disabled}" early_exit_less disabled_less)
if(disabled_less MATCHES "[	 ]vptest[	 ]")
    message(FATAL_ERROR
        "-fno-tree-early-exit-vectorize did not disable its pass\n"
        "${disabled_less}")
endif()

compile_assembly("${OUTPUT}.independent.s" -O3 -march=x86-64-v3
                 -fno-tree-loop-vectorize)
file(READ "${OUTPUT}.independent.s" independent)
extract_function("${independent}" early_exit_less independent_less)
if(NOT independent_less MATCHES "[\t ]vptest[\t ]")
    message(FATAL_ERROR
        "ordinary and early-exit loop vectorizers are not independently selectable\n"
        "${independent_less}")
endif()

execute_process(
    COMMAND "${CC}" -c -O3 -march=x86-64-v3
            "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "early-exit object emission failed (${object_status})\n"
        "${object_stdout}\n${object_stderr}")
endif()
