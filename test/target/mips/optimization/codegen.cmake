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

function(compile_reassociation_variant name balance)
    execute_process(
        COMMAND "${CC}" -S -O3 -mprofile=vr4300-o32
                "-mrisc-cisc-balance=${balance}" ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${name} MIPS reassociation compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(compile_unroll_variant name balance)
    execute_process(
        COMMAND "${CC}" -S -O3 -mprofile=vr4300-o32 -funroll-loops
                -funroll-factor=4 "-mrisc-cisc-balance=${balance}"
                ${ARGN} "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${name} MIPS unroll compilation failed\n${stdout}\n${stderr}")
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
compile_variant(grouped_select -fif-conversion-limit=12)
compile_variant(grouped_select_no_fusion -fif-conversion-limit=12
                -fno-compare-select-fusion)
compile_variant(no_delay -fno-schedule-insns2)
compile_variant(no_machine_combine -fno-machine-combine)
compile_variant(no_bit_ccp -fno-tree-bit-ccp)
compile_variant(no_if_conversion -fno-if-conversion)
compile_variant(no_memory_if_conversion -fif-conversion-memory-limit=0)
compile_variant(force_ordinary_if_conversion -fif-conversion-limit=12)
compile_reassociation_variant(reassoc_risc 0)
compile_reassociation_variant(reassoc_risc_off 0 -fno-tree-reassoc)
compile_reassociation_variant(reassoc_cisc 100 -fno-unroll-loops)
compile_reassociation_variant(reassoc_cisc_off 100 -fno-unroll-loops
                              -fno-tree-reassoc)
compile_unroll_variant(unroll_risc 0)
compile_unroll_variant(unroll_cisc 100)
compile_unroll_variant(unroll_risc_no_delay 0 -fno-schedule-insns2)
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

function_body("${OUTPUT}.enabled.s"
              mips_integer_immediates integer_immediates)
foreach(pattern
        "[	 ]addiu[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,17"
        "[	 ]xori[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,255"
        "[	 ]srl[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,13"
        "[	 ]addiu[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,-32768"
        "[	 ]andi[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,65535")
    if(NOT integer_immediates MATCHES "${pattern}")
        message(FATAL_ERROR
            "MIPS integer immediate selection is missing '${pattern}'\n"
            "${integer_immediates}")
    endif()
endforeach()
function_body("${OUTPUT}.no_machine_combine.s"
              mips_integer_immediates register_constants)
if(register_constants MATCHES
       "[	 ](addiu|xori|andi)[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,(17|255|-32768|65535)" OR
   register_constants MATCHES
       "[	 ]srl[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+,13")
    message(FATAL_ERROR
        "-fno-machine-combine did not preserve register constants\n"
        "${register_constants}")
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

function_body("${OUTPUT}.reassoc_risc.s"
              mips_reassociation_pressure reassoc_risc)
function_body("${OUTPUT}.reassoc_risc_off.s"
              mips_reassociation_pressure reassoc_risc_off)
if(NOT reassoc_risc STREQUAL reassoc_risc_off)
    message(FATAL_ERROR
        "the RISC endpoint accepted a pressure-increasing reassociation\n"
        "enabled:\n${reassoc_risc}\ndisabled:\n${reassoc_risc_off}")
endif()
function_body("${OUTPUT}.reassoc_cisc.s"
              mips_reassociation_without_unroll reassoc_cisc)
function_body("${OUTPUT}.reassoc_cisc_off.s"
              mips_reassociation_without_unroll reassoc_cisc_off)
if(reassoc_cisc STREQUAL reassoc_cisc_off)
    message(FATAL_ERROR
        "-ftree-reassoc depended on loop unrolling or ignored the CISC endpoint")
endif()

function_body("${OUTPUT}.grouped_select.s"
              mips_grouped_select grouped_select)
function_body("${OUTPUT}.grouped_select_no_fusion.s"
              mips_grouped_select separate_selects)
string(REGEX MATCHALL "[	 ]beq[	 ]" grouped_select_branches
             "${grouped_select}")
string(REGEX MATCHALL "[	 ]beq[	 ]" separate_select_branches
             "${separate_selects}")
list(LENGTH grouped_select_branches grouped_select_branch_count)
list(LENGTH separate_select_branches separate_select_branch_count)
if(NOT grouped_select_branch_count EQUAL 1 OR
   NOT separate_select_branch_count EQUAL 2)
    message(FATAL_ERROR
        "same-condition MIPS selects did not share exactly one branch\n"
        "enabled (${grouped_select_branch_count}):\n${grouped_select}\n"
        "disabled (${separate_select_branch_count}):\n${separate_selects}")
endif()

function_body("${OUTPUT}.unroll_risc.s"
              mips_store_pressure pressure_risc)
function_body("${OUTPUT}.unroll_cisc.s"
              mips_store_pressure pressure_cisc)
if(pressure_risc STREQUAL pressure_cisc)
    message(FATAL_ERROR
        "the RISC endpoint did not reduce pressure-heavy store unrolling")
endif()
if(NOT pressure_risc MATCHES
       "[\t ]li[\t ]+\\\$[a-z0-9]+,4294967294" OR
   NOT pressure_cisc MATCHES
       "[\t ]li[\t ]+\\\$[a-z0-9]+,4294967292")
    message(FATAL_ERROR
        "store-loop pressure did not select factors two and four\n"
        "RISC:\n${pressure_risc}\nCISC:\n${pressure_cisc}")
endif()
string(REGEX MATCH
       "\\.frame[\t ]+\\\$sp,([0-9]+),\\\$ra"
       pressure_risc_frame "${pressure_risc}")
set(pressure_risc_frame_size "${CMAKE_MATCH_1}")
string(REGEX MATCH
       "\\.frame[\t ]+\\\$sp,([0-9]+),\\\$ra"
       pressure_cisc_frame "${pressure_cisc}")
set(pressure_cisc_frame_size "${CMAKE_MATCH_1}")
if(pressure_risc_frame STREQUAL "" OR pressure_cisc_frame STREQUAL "" OR
   pressure_risc_frame_size GREATER pressure_cisc_frame_size)
    message(FATAL_ERROR
        "the RISC store-loop frame exceeded the CISC frame\n"
        "RISC (${pressure_risc_frame_size}):\n${pressure_risc}\n"
        "CISC (${pressure_cisc_frame_size}):\n${pressure_cisc}")
endif()

function_body("${OUTPUT}.unroll_risc.s"
              mips_compact_store compact_risc)
function_body("${OUTPUT}.unroll_cisc.s"
              mips_compact_store compact_cisc)
if(NOT compact_risc MATCHES "[\t ]li[\t ]+\\\$[a-z0-9]+,4" OR
   NOT compact_cisc MATCHES "[\t ]li[\t ]+\\\$[a-z0-9]+,4")
    message(FATAL_ERROR
        "an endpoint reduced a compact store loop below factor four\n"
        "RISC:\n${compact_risc}\nCISC:\n${compact_cisc}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_high_bit_select high_bit_select)
function_body("${OUTPUT}.no_memory_if_conversion.s"
              mips_high_bit_select high_bit_branch)
if(high_bit_select STREQUAL high_bit_branch)
    message(FATAL_ERROR
        "the MIPS profile did not if-convert an unpredictable top-bit test")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_range_select range_default)
function_body("${OUTPUT}.no_if_conversion.s"
              mips_range_select range_branch)
function_body("${OUTPUT}.force_ordinary_if_conversion.s"
              mips_range_select range_forced)
if(NOT range_default STREQUAL range_branch OR
   range_default STREQUAL range_forced)
    message(FATAL_ERROR
        "the MIPS profile did not preserve an ordinary range branch or "
        "honor -fif-conversion-limit\n"
        "default:\n${range_default}\nforced:\n${range_forced}")
endif()

function_body("${OUTPUT}.unroll_risc.s"
              mips_phi_copy_edge likely_phi_edge)
function_body("${OUTPUT}.unroll_risc_no_delay.s"
              mips_phi_copy_edge ordinary_phi_edge)
if(NOT likely_phi_edge MATCHES
       "[\t ]b(eq|ne)zl[^\n]*\n[\t ]+move[\t ]" OR
   ordinary_phi_edge MATCHES "[\t ]b(eq|ne)zl[\t ]")
    message(FATAL_ERROR
        "a single MIPS PHI-edge copy did not use an annulled delay slot\n"
        "enabled:\n${likely_phi_edge}\ndisabled:\n${ordinary_phi_edge}")
endif()
