# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_variant name)
    execute_process(
        COMMAND "${CC}" -S -O2 -mprofile=vr4300-o32 -fno-unroll-loops
                ${ARGN} "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${name} MIPS optimization compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(function_body output symbol result)
    file(READ "${output}" assembly)
    string(FIND "${assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${output} does not define ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    if(length EQUAL -1)
        message(FATAL_ERROR "${output} does not terminate ${symbol}")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${result} "${body}" PARENT_SCOPE)
endfunction()

compile_variant(enabled)
compile_variant(no_iv -fno-ivopts)
compile_variant(no_tail -fno-optimize-sibling-calls)
compile_variant(no_fusion -fno-compare-branch-fusion)
compile_variant(no_delay -fno-schedule-insns2)
compile_variant(no_bit_ccp -fno-tree-bit-ccp)
execute_process(
    COMMAND "${CC}" -S -O2 -target mipsel-unknown-elf -mabi=o32
            -march=vr4300 -fno-unroll-loops "${SOURCE}"
            -o "${OUTPUT}.little.s"
    RESULT_VARIABLE little_status
    OUTPUT_VARIABLE little_stdout
    ERROR_VARIABLE little_stderr)
if(NOT little_status EQUAL 0)
    message(FATAL_ERROR
        "little-endian MIPS optimization compilation failed\n"
        "${little_stdout}\n${little_stderr}")
endif()

function_body("${OUTPUT}.enabled.s" mips_affine_sum affine)
foreach(pattern
        "[	 ]ld[	 ][^\n]*,0\\(\\$[a-z0-9]+\\)"
        "[	 ]ld[	 ][^\n]*,8\\(\\$[a-z0-9]+\\)"
        "[	 ]addiu[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,16")
    if(NOT affine MATCHES "${pattern}")
        message(FATAL_ERROR
            "affine pointer induction is missing '${pattern}'\n${affine}")
    endif()
endforeach()
function_body("${OUTPUT}.no_iv.s" mips_affine_sum scalar_affine)
if(NOT scalar_affine MATCHES "[	 ]sll[	 ][^\n]*,3")
    message(FATAL_ERROR
        "-fno-ivopts did not preserve scaled indexed addressing\n${scalar_affine}")
endif()

function_body("${OUTPUT}.enabled.s" mips_tail_wrapper tail)
if(NOT tail MATCHES "[	 ]j[	 ]+mips_tail_leaf" OR
   tail MATCHES "[	 ]jal[	 ]+mips_tail_leaf")
    message(FATAL_ERROR "sibling-call lowering is missing\n${tail}")
endif()
function_body("${OUTPUT}.no_tail.s" mips_tail_wrapper ordinary_call)
if(NOT ordinary_call MATCHES "[	 ]jal[	 ]+mips_tail_leaf")
    message(FATAL_ERROR
        "-fno-optimize-sibling-calls did not preserve a call\n${ordinary_call}")
endif()

function_body("${OUTPUT}.enabled.s" mips_equal_branch fused)
if(NOT fused MATCHES
   "[	 ]b(eq|ne)[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+," OR
   fused MATCHES "[	 ]sltiu[	 ]")
    message(FATAL_ERROR "direct comparison branch is missing\n${fused}")
endif()
function_body("${OUTPUT}.no_fusion.s" mips_equal_branch separate_compare)
if(NOT separate_compare MATCHES "[	 ]xor[	 ]" OR
   NOT separate_compare MATCHES "[	 ]sltiu[	 ]")
    message(FATAL_ERROR
        "-fno-compare-branch-fusion did not preserve the comparison value\n${separate_compare}")
endif()

function_body("${OUTPUT}.enabled.s" mips_delay_branch scheduled_delay)
function_body("${OUTPUT}.no_delay.s" mips_delay_branch empty_delay)
if(NOT scheduled_delay MATCHES
   "[	 ]b(eq|ne)[^\n]*\n[	 ]+(addiu|addu|subu)[	 ]")
    message(FATAL_ERROR "a safe branch delay slot was not filled\n${scheduled_delay}")
endif()
if(NOT empty_delay MATCHES "[	 ]b(eq|ne)[^\n]*\n[	 ]+nop")
    message(FATAL_ERROR
        "-fno-schedule-insns2 did not leave the delay slot empty\n${empty_delay}")
endif()

function_body("${OUTPUT}.enabled.s" mips_narrow_mask narrow_big)
if(NOT narrow_big MATCHES "[	 ]lwu[	 ][^\n]*,4\\(\\$[a-z0-9]+\\)" OR
   narrow_big MATCHES "[	 ]ld[	 ]")
    message(FATAL_ERROR
        "big-endian masked truncation was not narrowed at +4\n${narrow_big}")
endif()
function_body("${OUTPUT}.little.s" mips_narrow_mask narrow_little)
if(NOT narrow_little MATCHES "[	 ]lwu[	 ][^\n]*,0\\(\\$[a-z0-9]+\\)" OR
   narrow_little MATCHES "[	 ]ld[	 ]")
    message(FATAL_ERROR
        "little-endian masked truncation was not narrowed at +0\n${narrow_little}")
endif()
function_body("${OUTPUT}.no_bit_ccp.s" mips_narrow_mask wide_load)
if(NOT wide_load MATCHES "[	 ]ld[	 ][^\n]*,0\\(\\$[a-z0-9]+\\)")
    message(FATAL_ERROR
        "-fno-tree-bit-ccp did not preserve the wide load\n${wide_load}")
endif()
function_body("${OUTPUT}.enabled.s" mips_volatile_narrow_mask volatile_load)
if(NOT volatile_load MATCHES "[	 ]ld[	 ][^\n]*,0\\(\\$[a-z0-9]+\\)")
    message(FATAL_ERROR
        "bit propagation changed a volatile load width\n${volatile_load}")
endif()
