# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_variant name)
    execute_process(
        COMMAND "${CC}" -S -O2 -funwind-tables -mprofile=vr4300-o32 -fno-unroll-loops
                ${ARGN} "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${name} MIPS optimization compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(compile_size_variant name)
    execute_process(
        COMMAND "${CC}" -S -Oz -funwind-tables -mprofile=vr4300-o32 -fno-unroll-loops
                ${ARGN} "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${name} MIPS size compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(compile_reassociation_variant name balance)
    execute_process(
        COMMAND "${CC}" -S -O3 -funwind-tables -mprofile=vr4300-o32
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
        COMMAND "${CC}" -S -O3 -funwind-tables -mprofile=vr4300-o32 -funroll-loops
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
compile_variant(no_rotation -fno-tree-loop-rotate)
compile_variant(grouped_select -fif-conversion-limit=12)
compile_variant(grouped_select_no_fusion -fif-conversion-limit=12
                -fno-compare-select-fusion)
compile_variant(no_delay -fno-schedule-insns2)
compile_variant(no_schedule -fno-schedule-insns)
compile_variant(no_machine_combine -fno-machine-combine)
compile_variant(no_address_combine -fno-combine-addresses)
compile_variant(no_slsr -fno-tree-slsr)
compile_variant(no_ccp -fno-tree-ccp)
compile_variant(no_licm -fno-move-loop-invariants)
compile_variant(no_ipa_ra -fno-ipa-ra)
compile_variant(cisc -mrisc-cisc-balance=100)
compile_variant(fix4300 -mfix4300)
compile_variant(fix4300_no_schedule -mfix4300 -fno-schedule-insns)
compile_variant(r4000 -march=r4000 -mtune=r4000)
compile_variant(r3000 -march=r3000 -mtune=r3000)
compile_variant(no_cprop -fno-cprop-registers)
compile_variant(no_bit_ccp -fno-tree-bit-ccp)
compile_variant(no_if_conversion -fno-if-conversion)
compile_variant(no_reassoc -fno-tree-reassoc)
compile_variant(console -mprofile=n64)
compile_variant(no_ivopts -fno-ivopts)
compile_variant(no_memory_if_conversion -fif-conversion-memory-limit=0)
compile_variant(force_memory_if_conversion -fif-conversion-memory-limit=12)
compile_variant(force_ordinary_if_conversion -fif-conversion-limit=12)
compile_variant(no_reorder -fno-reorder-blocks)
compile_size_variant(size)
compile_size_variant(size_no_machine_combine -fno-machine-combine)
compile_size_variant(size_no_licm -fno-move-loop-invariants)
compile_reassociation_variant(reassoc_risc 0)
compile_reassociation_variant(reassoc_risc_off 0 -fno-tree-reassoc)
compile_reassociation_variant(reassoc_cisc 100 -fno-unroll-loops)
compile_reassociation_variant(reassoc_cisc_off 100 -fno-unroll-loops
                              -fno-tree-reassoc)
compile_unroll_variant(unroll_risc 0)
compile_unroll_variant(unroll_cisc 100)
compile_unroll_variant(unroll_risc_no_delay 0 -fno-schedule-insns2)
compile_unroll_variant(unroll_risc_no_iv 0 -fno-ivopts)
compile_unroll_variant(unroll_risc_no_reorder 0 -fno-reorder-blocks
                       -fno-tree-loop-rotate)
compile_unroll_variant(unroll_risc_no_reorder_no_delay 0
                       -fno-reorder-blocks -fno-schedule-insns2
                       -fno-tree-loop-rotate)
execute_process(
    COMMAND "${CC}" -S -O2 -funwind-tables -target mipsel-unknown-elf -mabi=o32
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

function_body("${OUTPUT}.enabled.s" mips_shared_epilogue shared_epilogue)
if(shared_epilogue MATCHES
       "[	 ]b[	 ]+\\.Lcross[.]mips[.][0-9_]+[.]return" OR
   NOT shared_epilogue MATCHES
       "\\.Lcross[.]mips[.][0-9_]+[.]return:\n" OR
   NOT shared_epilogue MATCHES "[.]cfi_restore")
    message(FATAL_ERROR
        "a final return should fall through to its CFI epilogue\n"
        "${shared_epilogue}")
endif()
function_body("${OUTPUT}.enabled.s" mips_multiple_epilogues multiple_epilogues)
string(REGEX MATCHALL
       "[	 ]b[	 ]+\\.Lcross[.]mips[.][0-9_]+[.]return"
       epilogue_branches "${multiple_epilogues}")
list(LENGTH epilogue_branches epilogue_branch_count)
if(NOT epilogue_branch_count EQUAL 1)
    message(FATAL_ERROR
        "only non-final returns should branch to the shared epilogue\n"
        "${multiple_epilogues}")
endif()

function_body("${OUTPUT}.enabled.s" mips_equal_branch fused)
if(NOT fused MATCHES
   "[	 ]b(eq|ne)l?[	 ]+\\$[a-z0-9]+,\\$[a-z0-9]+," OR
   fused MATCHES "[	 ]sltiu[	 ]")
    message(FATAL_ERROR "direct comparison branch is missing\n${fused}")
endif()
function_body("${OUTPUT}.no_fusion.s" mips_equal_branch separate_compare)
if(NOT separate_compare MATCHES "[	 ]xor[	 ]" OR
   NOT separate_compare MATCHES "[	 ]sltiu[	 ]")
    message(FATAL_ERROR
        "-fno-compare-branch-fusion did not preserve the comparison value\n${separate_compare}")
endif()

function_body("${OUTPUT}.enabled.s" mips_rotated_outer rotated_outer)
function_body("${OUTPUT}.no_fusion.s" mips_rotated_outer separate_rotated_compare)
function_body("${OUTPUT}.no_rotation.s" mips_rotated_outer unrotated_outer)
if(rotated_outer MATCHES "[\t ]xor[\t ]" OR
   NOT rotated_outer MATCHES
       "[\t ]b(eq|ne)l?[\t ]+\\$[a-z0-9]+,\\$[a-z0-9]+," OR
   NOT separate_rotated_compare MATCHES "[\t ]xor[\t ]" OR
   NOT separate_rotated_compare MATCHES "[\t ]sltu[\t ]" OR
   rotated_outer STREQUAL unrotated_outer)
    message(FATAL_ERROR
        "rotated two-PHI edges did not retain direct comparison branches\n"
        "enabled:\n${rotated_outer}\nno fusion:\n${separate_rotated_compare}\n"
        "no rotation:\n${unrotated_outer}")
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

# A store fills its own arm's jump slot, and an arm of one store runs in
# the branch's branch-likely slot.
function_body("${OUTPUT}.enabled.s"
              mips_successor_delay scheduled_store_arm)
function_body("${OUTPUT}.enabled.s"
              mips_successor_compare forwarded_store_arm)
function_body("${OUTPUT}.no_delay.s"
              mips_successor_compare empty_store_arm)
if(NOT scheduled_store_arm MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9_]+[.]bb[.][0-9]+\n[\t ]+sw[\t ]" OR
   NOT forwarded_store_arm MATCHES
       "[\t ]b(eq|ne)z?l[\t ][^\n]*\n[\t ]+sw[\t ]" OR
   empty_store_arm MATCHES "[\t ]b[a-z]*[\t ][^\n]*\n[\t ]+sw[\t ]")
    message(FATAL_ERROR
        "a store did not fill its arm's delay slot or -fno-schedule-insns2 "
        "was ignored\n"
        "scheduled:\n${scheduled_store_arm}\nforwarded:\n${forwarded_store_arm}\n"
        "disabled:\n${empty_store_arm}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_successor_call_delay scheduled_successor)
function_body("${OUTPUT}.no_delay.s"
              mips_successor_call_delay empty_successor)
if(NOT scheduled_successor MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9_]+[.]tmp[.][0-9]+\n[\t ]+xori[\t ]" OR
   NOT scheduled_successor MATCHES
       "[\t ]xori[^\n]*\n\\.Lcross[.]mips[.][0-9_]+[.]tmp[.][0-9]+:\n[\t ]+(jr|b)[\t ][^\n]*\n[\t ]+addiu[\t ]" OR
   NOT empty_successor MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9_]+[.]bb[.][0-9]+\n[\t ]+nop")
    message(FATAL_ERROR
        "an unconditional edge did not execute a safe successor operation "
        "in its delay slot or -fno-schedule-insns2 was ignored\n"
        "scheduled:\n${scheduled_successor}\ndisabled:\n${empty_successor}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_successor_call_compare scheduled_successor_compare)
function_body("${OUTPUT}.no_delay.s"
              mips_successor_call_compare empty_successor_compare)
if(NOT scheduled_successor_compare MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9_]+[.]tmp[.][0-9]+\n[\t ]+sltu?[\t ]" OR
   NOT scheduled_successor_compare MATCHES
       "[\t ]sltu?[^\n]*\n\\.Lcross[.]mips[.][0-9_]+[.]tmp[.][0-9]+:\n[\t ]+b(eq|ne)z?l?[\t ]" OR
   NOT empty_successor_compare MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9_]+[.]bb[.][0-9]+\n[\t ]+nop")
    message(FATAL_ERROR
        "a fused successor comparison did not fill an unconditional edge "
        "delay slot or -fno-schedule-insns2 was ignored\n"
        "scheduled:\n${scheduled_successor_compare}\n"
        "disabled:\n${empty_successor_compare}")
endif()

function_body("${OUTPUT}.unroll_risc.s"
              mips_copy_edge_delay scheduled_copy_edge)
function_body("${OUTPUT}.unroll_risc_no_delay.s"
              mips_copy_edge_delay empty_copy_edge)
if(NOT scheduled_copy_edge MATCHES
       "[\t ]beq[\t ][^\n]*\n[\t ]+addu[\t ][^\n]*\n[\t ]+move[\t ]" OR
   NOT empty_copy_edge MATCHES
       "[\t ]beq[\t ][^\n]*\n[\t ]+nop\n[\t ]+move[\t ]")
    message(FATAL_ERROR
        "a branch whose other edge carries copies did not fill its delay "
        "slot or -fno-schedule-insns2 was ignored\n"
        "scheduled:\n${scheduled_copy_edge}\ndisabled:\n${empty_copy_edge}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_fp_latency_schedule scheduled_fp)
function_body("${OUTPUT}.no_schedule.s"
              mips_fp_latency_schedule serial_fp)
if(NOT scheduled_fp MATCHES
       "[\t ]mul[.]d[^\n]*\n[\t ]+ldc1[^\n]*\n[\t ]+ldc1[^\n]*\n[\t ]+mul[.]d" OR
   scheduled_fp MATCHES "[\t ]sdc1[\t ]+\\$f(0|2|4)," OR
   NOT serial_fp MATCHES
       "[\t ]mul[.]d[^\n]*\n[\t ]+add[.]d")
    message(FATAL_ERROR
        "MIPS FP latency scheduling did not overlap independent products "
        "without spills or honor -fno-schedule-insns\n"
        "scheduled:\n${scheduled_fp}\nserial:\n${serial_fp}")
endif()
if(NOT scheduled_fp MATCHES
       "[\t ]dmtc1[\t ]+\\$zero,\\$f[0-9]+\n[\t ]+nop\n[\t ]+add[.]d" OR
   scheduled_fp MATCHES
       "[\t ]li[\t ][^\n]*,0\n[\t ]+dmtc1")
    message(FATAL_ERROR
        "MIPS zero floating constant did not use the architectural zero "
        "register or lost its required transfer delay\n${scheduled_fp}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_fp_mul_pair fp_result_pair)
if(fp_result_pair MATCHES
       "[\t ]dmtc1[^\n]*\n[\t ]+nop" OR
   NOT fp_result_pair MATCHES
       "[\t ]dmtc1[^\n]*\n[\t ]+(move|mul[.]d)" OR
   NOT fp_result_pair MATCHES
       "[\t ]add[.]d[\t ]+\\$f0," OR
   fp_result_pair MATCHES
       "[\t ]mov[.]d[\t ]+\\$f0,")
    message(FATAL_ERROR
        "MIPS floating result recoloring or dependency-aware transfer "
        "scheduling regressed\n${fp_result_pair}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_fp_fixed_result fp_fixed_result)
if(NOT fp_fixed_result MATCHES "[\t ]c[.]lt[.]d[\t ]" OR
   NOT fp_fixed_result MATCHES "[\t ]mov[.]d[\t ]+\\$f0,")
    message(FATAL_ERROR
        "MIPS fixed-F0 floating operations were unsafely recolored\n"
        "${fp_fixed_result}")
endif()

function_body("${OUTPUT}.size.s" mips_fp_literal pooled_literal)
function_body("${OUTPUT}.size_no_machine_combine.s"
              mips_fp_literal materialized_literal)
file(READ "${OUTPUT}.size.s" size_assembly)
file(READ "${OUTPUT}.size_no_machine_combine.s"
          materialized_assembly)
if(NOT pooled_literal MATCHES
       "[\t ]lui[\t ][^\n]*%hi[(]\\.Lcross[.]mips[.][0-9_]+[.]float[.][0-9]+[)]" OR
   NOT pooled_literal MATCHES
       "[\t ]ldc1[\t ][^\n]*%lo[(]\\.Lcross[.]mips[.][0-9_]+[.]float[.][0-9]+[)]" OR
   NOT size_assembly MATCHES
       "\\.Lcross[.]mips[.][0-9_]+[.]float[.][0-9]+:\n[\t ]+\\.quad[\t ]" OR
   NOT materialized_literal MATCHES "[\t ]dsll[\t ]" OR
   materialized_assembly MATCHES
       "\\.Lcross[.]mips[.][0-9_]+[.]float[.][0-9]+:")
    message(FATAL_ERROR
        "MIPS size-mode floating literal pooling or "
        "-fno-machine-combine regressed\n"
        "pooled:\n${pooled_literal}\nmaterialized:\n${materialized_literal}")
endif()

function_body("${OUTPUT}.size.s"
              mips_integer_literal pooled_integer)
function_body("${OUTPUT}.size_no_machine_combine.s"
              mips_integer_literal materialized_integer)
if(NOT pooled_integer MATCHES
       "[\t ]lui[\t ][^\n]*%hi[(]\\.Lcross[.]mips[.][0-9_]+[.]integer[.][0-9]+[)]" OR
   NOT pooled_integer MATCHES
       "[\t ]ld[\t ][^\n]*%lo[(]\\.Lcross[.]mips[.][0-9_]+[.]integer[.][0-9]+[)]" OR
   NOT size_assembly MATCHES
       "\\.Lcross[.]mips[.][0-9_]+[.]integer[.][0-9]+:\n[\t ]+\\.quad[\t ]" OR
   NOT materialized_integer MATCHES "[\t ]dsll[\t ]" OR
   materialized_assembly MATCHES
       "\\.Lcross[.]mips[.][0-9_]+[.]integer[.][0-9]+:")
    message(FATAL_ERROR
        "MIPS size-mode integer literal pooling or "
        "-fno-machine-combine regressed\n"
        "pooled:\n${pooled_integer}\n"
        "materialized:\n${materialized_integer}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_fp_delay_slot fp_delay_slot)
function_body("${OUTPUT}.no_delay.s"
              mips_fp_delay_slot fp_empty_delay)
if(NOT fp_delay_slot MATCHES
       "[\t ]b(eq|ne)[^\n]*\n[\t ]+add[.]d[\t ]" OR
   fp_empty_delay MATCHES
       "[\t ]b(eq|ne)[^\n]*\n[\t ]+add[.]d[\t ]")
    message(FATAL_ERROR
        "MIPS floating arithmetic did not fill a safe branch delay slot or "
        "-fno-schedule-insns2 was ignored\n"
        "scheduled:\n${fp_delay_slot}\ndisabled:\n${fp_empty_delay}")
endif()

function_body("${OUTPUT}.fix4300.s"
              mips_fp_mul_delay_slot fixed_fp_multiply)
if(fixed_fp_multiply MATCHES
       "[\t ]b(eq|ne)[^\n]*\n[\t ]+mul[.]d[\t ]" OR
   fixed_fp_multiply MATCHES
       "[\t ]mul[.]d[^\n]*\n[\t ]+nop")
    message(FATAL_ERROR
        "-mfix4300 split a multiply workaround across a branch or failed "
        "to use intervening work\n"
        "${fixed_fp_multiply}")
endif()

function_body("${OUTPUT}.fix4300_no_schedule.s"
              mips_fp_mul_pair consecutive_fp_multiplies)
if(NOT consecutive_fp_multiplies MATCHES
       "[\t ]mul[.]d[^\n]*\n[\t ]+nop\n[\t ]+mul[.]d")
    message(FATAL_ERROR
        "-mfix4300 did not separate consecutive floating multiplies\n"
        "${consecutive_fp_multiplies}")
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

function_body("${OUTPUT}.enabled.s"
              mips_shared_index_scale shared_index_scale)
function_body("${OUTPUT}.no_address_combine.s"
              mips_shared_index_scale separate_index_scales)
string(REGEX MATCHALL "[\t ]sll[\t ][^\n]*,3"
       shared_index_shifts "${shared_index_scale}")
string(REGEX MATCHALL "[\t ]sll[\t ][^\n]*,3"
       separate_index_shifts "${separate_index_scales}")
list(LENGTH shared_index_shifts shared_index_shift_count)
list(LENGTH separate_index_shifts separate_index_shift_count)
if(NOT shared_index_shift_count EQUAL 1 OR
   NOT separate_index_shift_count EQUAL 2)
    message(FATAL_ERROR
        "MIPS address combining did not share a repeated scaled index or "
        "-fno-combine-addresses was ignored\n"
        "combined:\n${shared_index_scale}\n"
        "separate:\n${separate_index_scales}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_ipa_call_loop precise_private_call)
function_body("${OUTPUT}.no_ipa_ra.s"
              mips_ipa_call_loop conservative_private_call)
if(precise_private_call MATCHES
       "[\t ](sd|ld)[\t ]+\\$s[0-7]," OR
   NOT precise_private_call MATCHES "[\t ]jal[\t ]" OR
   NOT conservative_private_call MATCHES
       "[\t ](sd|ld)[\t ]+\\$s[0-7],")
    message(FATAL_ERROR
        "MIPS private-call clobber summaries or -fno-ipa-ra regressed\n"
        "precise:\n${precise_private_call}\n"
        "conservative:\n${conservative_private_call}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_small_constant_multiply reduced_multiply)
function_body("${OUTPUT}.no_slsr.s"
              mips_small_constant_multiply hardware_multiply)
if(NOT reduced_multiply MATCHES
       "[\t ]dsll[\t ][^\n]*,4" OR
   NOT reduced_multiply MATCHES "[\t ]daddu[\t ]" OR
   reduced_multiply MATCHES "[\t ]dmult[\t ]" OR
   NOT hardware_multiply MATCHES "[\t ]dmult[\t ]" OR
   NOT hardware_multiply MATCHES "[\t ]mflo[\t ]")
    message(FATAL_ERROR
        "MIR constant-multiply strength reduction is missing or "
        "-fno-tree-slsr was ignored\n"
        "reduced:\n${reduced_multiply}\nhardware:\n${hardware_multiply}")
endif()

# A signed product reduces in the unsigned type of its width, where shifts
# and additions wrap as the multiplication does.
function_body("${OUTPUT}.enabled.s"
              mips_signed_constant_multiply signed_multiply)
if(NOT signed_multiply MATCHES "[\t ]dsll[\t ][^\n]*,4" OR
   NOT signed_multiply MATCHES "[\t ]daddu[\t ]" OR
   signed_multiply MATCHES "[\t ]dmult[\t ]")
    message(FATAL_ERROR
        "MIR strength reduction did not reduce a signed multiplication\n"
        "${signed_multiply}")
endif()

function_body("${OUTPUT}.no_ccp.s"
              mips_untyped_constant_multiply independent_slsr)
function_body("${OUTPUT}.cisc.s"
              mips_small_constant_multiply cisc_multiply)
if(NOT independent_slsr MATCHES "[\t ]dsll[\t ][^\n]*,4" OR
   NOT independent_slsr MATCHES "[\t ]daddu[\t ]" OR
   independent_slsr MATCHES "[\t ]dmult[\t ]" OR
   NOT cisc_multiply MATCHES "[\t ]dmult[\t ]")
    message(FATAL_ERROR
        "MIR SLSR depends on CCP or ignored the RISC/CISC endpoint\n"
        "without CCP:\n${independent_slsr}\nCISC:\n${cisc_multiply}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_hilo_latency scheduled_hilo)
function_body("${OUTPUT}.no_schedule.s"
              mips_hilo_latency adjacent_hilo)
function_body("${OUTPUT}.r4000.s"
              mips_hilo_latency r4000_hilo)
if(NOT scheduled_hilo MATCHES
       "[\t ]mult[\t ][^\n]*\n[\t ]+srl[\t ][^\n]*\n[\t ]+mflo[\t ]" OR
   NOT adjacent_hilo MATCHES
       "[\t ]mult[\t ][^\n]*\n[\t ]+mflo[\t ]" OR
   NOT r4000_hilo MATCHES
       "[\t ]mult[\t ][^\n]*\n[\t ]+mflo[\t ]")
    message(FATAL_ERROR
        "MIPS HI/LO splitting did not fill multiply latency or honor "
        "-fno-schedule-insns/R4000 erratum policy\n"
        "scheduled:\n${scheduled_hilo}\nadjacent:\n${adjacent_hilo}\n"
        "R4000:\n${r4000_hilo}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_projected_parameter projected_parameter)
function_body("${OUTPUT}.no_cprop.s"
              mips_projected_parameter assembled_parameter)
if(NOT projected_parameter MATCHES
       "[\t ]addiu[\t ][^\n]*,\\$a1,17" OR
   projected_parameter MATCHES "[\t ]move[\t ][^\n]*,\\$a1" OR
   projected_parameter MATCHES "[\t ](d?sll|d?srl|or)[\t ]" OR
   NOT assembled_parameter MATCHES "[\t ]or[\t ]")
    message(FATAL_ERROR
        "MIPS ABI boundary projection did not compute directly from the low "
        "o32 endpoint or honor -fno-cprop-registers\n"
        "projected:\n${projected_parameter}\nordinary:\n${assembled_parameter}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_shared_zero_extension factored_extension)
function_body("${OUTPUT}.no_bit_ccp.s"
              mips_shared_zero_extension separate_extensions)
string(REGEX MATCHALL "dsll32" factored_left_shifts
       "${factored_extension}")
string(REGEX MATCHALL "dsll32" separate_left_shifts
       "${separate_extensions}")
list(LENGTH factored_left_shifts factored_extension_count)
list(LENGTH separate_left_shifts separate_extension_count)
if(NOT factored_extension_count EQUAL 2 OR
   NOT separate_extension_count EQUAL 3)
    message(FATAL_ERROR
        "MIR bitwise factoring did not share a zero extension or honor "
        "-fno-tree-bit-ccp\n"
        "factored:\n${factored_extension}\n"
        "separate:\n${separate_extensions}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_mixed_xor_return mixed_return)
function_body("${OUTPUT}.enabled.s"
              mips_mixed_xor_return_commuted mixed_return_commuted)
function_body("${OUTPUT}.little.s"
              mips_mixed_xor_return mixed_return_little)
function_body("${OUTPUT}.no_machine_combine.s"
              mips_mixed_xor_return ordinary_mixed_return)
foreach(body mixed_return mixed_return_commuted)
    if(NOT ${body} MATCHES "[\t ]xor[\t ]+\\$v1," OR
       NOT ${body} MATCHES "[\t ]dsrl32[\t ]+\\$v0," OR
       NOT ${body} MATCHES
           "[\t ]jr[\t ]+\\$ra\n[\t ]+sll[\t ]+\\$v1,\\$v1,0")
        message(FATAL_ERROR
            "MIPS mixed-width return was not decomposed for big-endian o32\n"
            "${${body}}")
    endif()
endforeach()
if(NOT mixed_return_little MATCHES "[\t ]xor[\t ]+\\$v0," OR
   NOT mixed_return_little MATCHES "[\t ]dsrl32[\t ]+\\$v1," OR
   NOT mixed_return_little MATCHES
       "[\t ]jr[\t ]+\\$ra\n[\t ]+sll[\t ]+\\$v0,\\$v0,0")
    message(FATAL_ERROR
        "MIPS mixed-width return lost little-endian o32 piece order\n"
        "${mixed_return_little}")
endif()
string(REGEX MATCHALL "dsll32" mixed_return_left_shifts
       "${mixed_return}")
string(REGEX MATCHALL "dsll32" ordinary_mixed_return_left_shifts
       "${ordinary_mixed_return}")
list(LENGTH mixed_return_left_shifts mixed_return_extension_count)
list(LENGTH ordinary_mixed_return_left_shifts
     ordinary_mixed_return_extension_count)
if(NOT mixed_return_extension_count EQUAL 2 OR
   NOT ordinary_mixed_return_extension_count EQUAL 3)
    message(FATAL_ERROR
        "-fno-machine-combine did not preserve the materialized mixed-width "
        "return\ncombined:\n${mixed_return}\n"
        "ordinary:\n${ordinary_mixed_return}")
endif()

function_body("${OUTPUT}.enabled.s" mips_affine_exit unit_exit)
function_body("${OUTPUT}.no_iv.s" mips_affine_exit ordered_exit)
function_body("${OUTPUT}.no_delay.s" mips_affine_exit unscheduled_exit)
if(unit_exit MATCHES "[\t ]sltu[\t ]" OR
   NOT unit_exit MATCHES "[\t ]bnel?[\t ]" OR
   NOT ordered_exit MATCHES "[\t ]sltu[\t ]")
    message(FATAL_ERROR
        "MIR unit induction exit selection did not replace unsigned-less "
        "or honor -fno-ivopts\n"
        "selected:\n${unit_exit}\nordinary:\n${ordered_exit}")
endif()
if(NOT unit_exit MATCHES
       "[\t ]bnel[^\n]*\n[\t ]+mult[\t ]" OR
   NOT unscheduled_exit MATCHES
       "[\t ]bne[^\n]*\n[\t ]+nop\n" OR
   unscheduled_exit MATCHES "[\t ]bnel[\t ]")
    message(FATAL_ERROR
        "MIPS successor scheduling did not start HI/LO work in an annulled "
        "taken-edge slot or honor -fno-schedule-insns2\n"
        "scheduled:\n${unit_exit}\nunscheduled:\n${unscheduled_exit}")
endif()

function_body("${OUTPUT}.no_licm.s" mips_affine_exit rematerialized_exit)
if(NOT unit_exit MATCHES "[\t ]li[\t ][^\n]*,1664525" OR
   unit_exit MATCHES "\\.bb\\.[0-9]+:\n[\t ]+li[\t ][^\n]*,1664525" OR
   NOT rematerialized_exit MATCHES
       "\\.bb\\.[0-9]+:\n[\t ]+li[\t ][^\n]*,1664525")
    message(FATAL_ERROR
        "target-costed MIR LICM did not hoist a two-instruction MIPS "
        "constant or honor -fno-move-loop-invariants\n"
        "enabled:\n${unit_exit}\ndisabled:\n${rematerialized_exit}")
endif()

function_body("${OUTPUT}.enabled.s" mips_narrow_mask narrow_big)
if(NOT narrow_big MATCHES "[	 ]lw[	 ][^\n]*,4\\(\\$[a-z0-9]+\\)" OR
   narrow_big MATCHES "[	 ]ld[	 ]")
    message(FATAL_ERROR
        "big-endian masked truncation was not narrowed to a native "
        "sign-canonical word at +4\n${narrow_big}")
endif()
function_body("${OUTPUT}.little.s" mips_narrow_mask narrow_little)
if(NOT narrow_little MATCHES "[	 ]lw[	 ][^\n]*,0\\(\\$[a-z0-9]+\\)" OR
   narrow_little MATCHES "[	 ]ld[	 ]")
    message(FATAL_ERROR
        "little-endian masked truncation was not narrowed to a native "
        "sign-canonical word at +0\n${narrow_little}")
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
       "[\t ]li[\t ]+\\\$[a-z0-9]+,-2\n" OR
   NOT pressure_cisc MATCHES
       "[\t ]li[\t ]+\\\$[a-z0-9]+,-4\n")
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
if(NOT compact_risc MATCHES "[\t ]li[\t ]+\\\$[a-z0-9]+,-4\n" OR
   NOT compact_cisc MATCHES "[\t ]li[\t ]+\\\$[a-z0-9]+,-4\n")
    message(FATAL_ERROR
        "an endpoint reduced a compact store loop below factor four\n"
        "RISC:\n${compact_risc}\nCISC:\n${compact_cisc}")
endif()

# The VR4300 selects with a branch, so the preset memory budget converts no
# unpredictable top-bit test; an explicit budget still does.
function_body("${OUTPUT}.enabled.s"
              mips_high_bit_select high_bit_default)
function_body("${OUTPUT}.no_memory_if_conversion.s"
              mips_high_bit_select high_bit_branch)
function_body("${OUTPUT}.force_memory_if_conversion.s"
              mips_high_bit_select high_bit_select)
if(NOT high_bit_default STREQUAL high_bit_branch OR
   high_bit_select STREQUAL high_bit_branch)
    message(FATAL_ERROR
        "the VR4300 preset converted a top-bit test, or "
        "-fif-conversion-memory-limit=12 did not\n"
        "default:\n${high_bit_default}\nforced:\n${high_bit_select}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_range_select range_default)
function_body("${OUTPUT}.no_if_conversion.s"
              mips_range_select range_branch)
function_body("${OUTPUT}.force_ordinary_if_conversion.s"
              mips_range_select range_forced)
string(REGEX REPLACE "[.]tmp[.][0-9]+" ".tmp.N"
       range_default_shape "${range_default}")
string(REGEX REPLACE "[.]tmp[.][0-9]+" ".tmp.N"
       range_branch_shape "${range_branch}")
string(REGEX REPLACE "[.]tmp[.][0-9]+" ".tmp.N"
       range_forced_shape "${range_forced}")
if(NOT range_default_shape STREQUAL range_branch_shape OR
   range_default_shape STREQUAL range_forced_shape)
    message(FATAL_ERROR
        "the MIPS profile did not preserve an ordinary range branch or "
        "honor -fif-conversion-limit\n"
        "default:\n${range_default}\nforced:\n${range_forced}")
endif()

function_body("${OUTPUT}.unroll_risc_no_reorder.s"
              mips_affine_exit likely_phi_edge)
function_body("${OUTPUT}.unroll_risc_no_reorder_no_delay.s"
              mips_affine_exit ordinary_phi_edge)
if(NOT likely_phi_edge MATCHES
       "[\t ]b(eq|ne)l[^\n]*\n[\t ]+move[\t ]" OR
   ordinary_phi_edge MATCHES "[\t ]b(eq|ne)l[\t ]")
    message(FATAL_ERROR
        "a single MIPS PHI-edge copy did not use an annulled delay slot\n"
        "enabled:\n${likely_phi_edge}\ndisabled:\n${ordinary_phi_edge}")
endif()

function_body("${OUTPUT}.unroll_risc.s"
              mips_affine_exit affine_exit)
function_body("${OUTPUT}.unroll_risc_no_iv.s"
              mips_affine_exit counted_exit)
if(affine_exit MATCHES "[\t ]sltu[\t ]" OR
   NOT affine_exit MATCHES "[\t ]bne[\t ]" OR
   NOT counted_exit MATCHES "[\t ]sltu[\t ]")
    message(FATAL_ERROR
        "MIR affine-exit induction selection did not replace the loop counter "
        "or honor -fno-ivopts\n"
        "selected:\n${affine_exit}\nordinary:\n${counted_exit}")
endif()

# A quotient and remainder of the same operands share one division; scalar
# code before MIPS III keeps leaf values in caller-saved registers; and only
# the output pointers of an out-parameter function need frame homes.
function_body("${OUTPUT}.enabled.s" mips_paired_division paired_division)
function_body("${OUTPUT}.no_machine_combine.s" mips_paired_division separate_division)
function_body("${OUTPUT}.r3000.s" mips_paired_division mips1_division)
function_body("${OUTPUT}.enabled.s" mips_output_division output_division)
string(REGEX MATCHALL "[\t ]divu[\t ]" paired_divides "${paired_division}")
string(REGEX MATCHALL "[\t ]divu[\t ]" separate_divides "${separate_division}")
string(REGEX MATCHALL "[\t ]divu[\t ]" output_divides "${output_division}")
list(LENGTH paired_divides paired_count)
list(LENGTH separate_divides separate_count)
list(LENGTH output_divides output_count)
if(NOT paired_count EQUAL 1 OR NOT separate_count EQUAL 2 OR
   NOT output_count EQUAL 1 OR
   NOT paired_division MATCHES "[\t ]mflo[\t ]" OR
   NOT paired_division MATCHES "[\t ]mfhi[\t ]" OR
   mips1_division MATCHES "[\t ]s[wd][\t ]+\\$s[0-7]," OR
   output_division MATCHES "[\t ]s[wd][\t ]+\\$a[01],")
    message(FATAL_ERROR
        "MIPS division pairing, MIPS I leaf allocation, or output-parameter "
        "entry captures regressed\n"
        "paired:\n${paired_division}\nseparate:\n${separate_division}\n"
        "MIPS I:\n${mips1_division}\noutput:\n${output_division}")
endif()

# Global accesses add %lo in the load or store and share one %hi; an access
# that only ever uses one offset of an object folds that offset.
function_body("${OUTPUT}.enabled.s" mips_global_scalar global_scalar)
function_body("${OUTPUT}.no_address_combine.s" mips_global_scalar
              global_address)
string(REGEX MATCHALL "%hi[(]mips_quality_counter[)]" scalar_highs
       "${global_scalar}")
list(LENGTH scalar_highs scalar_high_count)
if(NOT scalar_high_count EQUAL 1 OR
   NOT global_scalar MATCHES
       "[\t ]lw[\t ][^\n]*,%lo[(]mips_quality_counter[)][(]" OR
   NOT global_scalar MATCHES
       "[\t ]sw[\t ][^\n]*,%lo[(]mips_quality_counter[)][(]" OR
   global_scalar MATCHES "addiu[^\n]*%lo" OR
   NOT global_address MATCHES
       "[\t ]addiu[\t ][^\n]*,%lo[(]mips_quality_counter[)]")
    message(FATAL_ERROR
        "MIPS global accesses did not share a %hi and fold %lo, or "
        "-fno-combine-addresses was ignored\n"
        "combined:\n${global_scalar}\nseparate:\n${global_address}")
endif()
function_body("${OUTPUT}.enabled.s" mips_global_field global_field)
function_body("${OUTPUT}.enabled.s" mips_global_index global_index)
if(NOT global_field MATCHES "%hi[(]mips_quality_pair[+]4[)]" OR
   NOT global_field MATCHES
       "[\t ]lw[\t ][^\n]*,%lo[(]mips_quality_pair[+]4[)][(]" OR
   NOT global_index MATCHES
       "[\t ]lw[\t ][^\n]*,%lo[(]mips_quality_table[)][(]" OR
   global_index MATCHES "addiu[^\n]*%lo")
    message(FATAL_ERROR
        "MIPS member or indexed global access did not fold its %lo\n"
        "member:\n${global_field}\nindexed:\n${global_index}")
endif()

# Comparisons with constants select SLTI/SLTIU (plus XORI for a complement)
# and need no mask on their 0/1 result.
function_body("${OUTPUT}.enabled.s" mips_compare_immediates
              compare_immediates)
function_body("${OUTPUT}.no_machine_combine.s" mips_compare_immediates
              compare_registers)
if(NOT compare_immediates MATCHES "[\t ]slti[\t ][^\n]*,\\$a0,100\n" OR
   NOT compare_immediates MATCHES
       "[\t ]sltiu[\t ][^\n]*,1001\n[\t ]+xori[\t ][^\n]*,1\n" OR
   NOT compare_immediates MATCHES "[\t ]sltiu[\t ][^\n]*,1\n" OR
   compare_immediates MATCHES "[\t ](li|andi)[\t ]" OR
   NOT compare_registers MATCHES "[\t ]slt[\t ]")
    message(FATAL_ERROR
        "MIPS comparison immediates or -fno-machine-combine regressed\n"
        "immediate:\n${compare_immediates}\nregister:\n${compare_registers}")
endif()
function_body("${OUTPUT}.enabled.s" mips_zero_branch zero_branch)
if(NOT zero_branch MATCHES "[\t ]b(ltz|gez)[\t ]+\\$a0," OR
   zero_branch MATCHES "[\t ]slt" OR
   NOT zero_branch MATCHES "[\t ]jr[\t ]+\\$ra\n[\t ]+addiu[\t ]")
    message(FATAL_ERROR
        "a MIPS comparison with zero did not branch on the register or "
        "fill its return delay slot\n${zero_branch}")
endif()

# Delay slots take an instruction from the block that falls into the
# branch, the call's last argument move, and an integer conversion reads
# its source register directly.
function_body("${OUTPUT}.enabled.s" mips_store_loop store_loop)
function_body("${OUTPUT}.no_delay.s" mips_store_loop store_loop_empty)
function_body("${OUTPUT}.enabled.s" mips_swap_call swap_call)
function_body("${OUTPUT}.no_delay.s" mips_swap_call swap_call_empty)
if(NOT store_loop MATCHES "[\t ]beq[^\n]*\n[\t ]+li[\t ]" OR
   NOT store_loop MATCHES "[\t ]bne[^\n]*\n[\t ]+addiu[\t ]" OR
   NOT store_loop_empty MATCHES "[\t ]bne[^\n]*\n[\t ]+nop\n" OR
   NOT swap_call MATCHES
       "[\t ]jal[\t ]+mips_global_index\n[\t ]+move[\t ]+\\$a0,\\$a1\n" OR
   NOT swap_call_empty MATCHES "[\t ]jal[\t ]+mips_global_index\n[\t ]+nop\n")
    message(FATAL_ERROR
        "MIPS cross-block or call delay slots regressed or "
        "-fno-schedule-insns2 was ignored\n"
        "loop:\n${store_loop}\nloop without delay slots:\n${store_loop_empty}\n"
        "call:\n${swap_call}\ncall without delay slots:\n${swap_call_empty}")
endif()
function_body("${OUTPUT}.enabled.s" mips_scaled_word scaled_word)
function_body("${OUTPUT}.no_schedule.s" mips_scaled_word scaled_word_fixed)
if(scaled_word MATCHES "[\t ]mtc1[^\n]*\n[\t ]+nop\n" OR
   NOT scaled_word_fixed MATCHES "[\t ]mtc1[^\n]*\n[\t ]+nop\n")
    message(FATAL_ERROR
        "MIPS scheduling did not fill an FPU transfer delay, or "
        "-fno-schedule-insns did\nscheduled:\n${scaled_word}\n"
        "unscheduled:\n${scaled_word_fixed}")
endif()
function_body("${OUTPUT}.enabled.s" mips_byte_result byte_result)
if(NOT byte_result MATCHES "[\t ]lbu[\t ]+\\$v0," OR
   byte_result MATCHES "[$]at")
    message(FATAL_ERROR
        "a loaded MIPS byte result was copied through $at\n${byte_result}")
endif()
function_body("${OUTPUT}.enabled.s" mips_int_to_float int_to_float)
if(NOT int_to_float MATCHES "[\t ]mtc1[\t ]+\\$a0,\\$f" OR
   int_to_float MATCHES "[\t ]move[\t ]+\\$t0")
    message(FATAL_ERROR
        "MIPS integer-to-float conversion staged its source through t0\n"
        "${int_to_float}")
endif()

# A one-operation arm runs in a branch-likely delay slot on its taken edge
# and the branch continues past it, and the loop's latch branches back to
# its header, so the loop has no unconditional jump.
function_body("${OUTPUT}.enabled.s" mips_ring_step ring_step)
function_body("${OUTPUT}.no_delay.s" mips_ring_step ring_step_no_delay)
if(NOT ring_step MATCHES "[\t ]bnel[\t ][^\n]*\n[\t ]+addiu[\t ]" OR
   NOT ring_step MATCHES "[\t ]bltzl[\t ][^\n]*\n[\t ]+addu[\t ]" OR
   ring_step MATCHES "[\t ]b[\t ]" OR
   ring_step_no_delay MATCHES "[\t ](bnel|bltzl)[\t ]")
    message(FATAL_ERROR
        "MIPS branch-likely arms or loop layout regressed, or "
        "-fno-schedule-insns2 was ignored\n"
        "enabled:\n${ring_step}\nwithout delay slots:\n${ring_step_no_delay}")
endif()
# A loop's register constants, including floating zero, are materialized
# before it under -fmove-loop-invariants.
function_body("${OUTPUT}.enabled.s" mips_constant_loop constant_loop)
function_body("${OUTPUT}.no_licm.s" mips_constant_loop constant_loop_inside)
if(constant_loop MATCHES "[\t ]mtc1[^\n]*\n[\t ]+nop\n" OR
   constant_loop MATCHES "[\t ]li[\t ][^\n]*,200\n[\t ]+beql" OR
   NOT constant_loop_inside MATCHES "[\t ]mtc1[\t ]+\\$zero[^\n]*\n[\t ]+nop\n" OR
   NOT constant_loop_inside MATCHES "[\t ]li[\t ][^\n]*,200\n[\t ]+beql")
    message(FATAL_ERROR
        "MIPS loop constants were not hoisted, or "
        "-fno-move-loop-invariants was ignored\n"
        "enabled:\n${constant_loop}\nwithout:\n${constant_loop_inside}")
endif()
# LHU and LB results need no extension and SLLV no amount mask.
function_body("${OUTPUT}.enabled.s" mips_narrow_sum narrow_sum)
function_body("${OUTPUT}.no_machine_combine.s" mips_narrow_sum
              narrow_sum_extended)
if(narrow_sum MATCHES ",65535\n" OR narrow_sum MATCHES "[\t ]sra[\t ]" OR
   narrow_sum MATCHES ",31\n" OR
   NOT narrow_sum MATCHES "[\t ]lhu[\t ]" OR
   NOT narrow_sum_extended MATCHES "[\t ]lhu[^\n]*\n[\t ]+andi[^\n]*,65535\n" OR
   NOT narrow_sum_extended MATCHES "[\t ]sra[\t ]")
    message(FATAL_ERROR
        "MIPS extended loads or masked shift amounts were normalized again, "
        "or -fno-machine-combine was ignored\n"
        "enabled:\n${narrow_sum}\nwithout:\n${narrow_sum_extended}")
endif()
# Neighboring elements are displacements from one address under
# -ftree-reassoc.
function_body("${OUTPUT}.enabled.s" mips_neighbors neighbors)
function_body("${OUTPUT}.no_reassoc.s" mips_neighbors neighbors_split)
if(NOT neighbors MATCHES "[\t ]lw[\t ][^\n]*,4\\(" OR
   NOT neighbors MATCHES "[\t ]lw[\t ][^\n]*,8\\(" OR
   neighbors_split MATCHES "[\t ]lw[\t ][^\n]*,4\\(")
    message(FATAL_ERROR
        "MIPS neighboring elements no longer share an address, or "
        "-fno-tree-reassoc was ignored\n"
        "enabled:\n${neighbors}\nwithout:\n${neighbors_split}")
endif()
# Under the N64 profile's trapping floating environment a comparison still
# fuses with its branch, which runs it in its place.
function_body("${OUTPUT}.console.s" mips_trapping_compare trapping_compare)
if(NOT trapping_compare MATCHES
       "[\t ]c\\.lt\\.s[^\n]*\n[\t ]+nop\n[\t ]+bc1[tf]l?[\t ]" OR
   trapping_compare MATCHES "[\t ]li[\t ][^\n]*,1\n")
    message(FATAL_ERROR
        "a trapping MIPS floating comparison materialized a boolean\n"
        "${trapping_compare}")
endif()
# Under the console profile a floating operation that may trap fills an
# integer branch's delay slot but does not move past a comparison that may
# trap.
function_body("${OUTPUT}.console.s" mips_trap_order trap_order)
if(NOT trap_order MATCHES "[\t ]beq[\t ][^\n]*\n[\t ]+add\\.s[\t ]" OR
   NOT trap_order MATCHES "[\t ]add\\.s[^\n]*\n[\t ]+c\\.lt\\.s[\t ]" OR
   trap_order MATCHES "[\t ]bc1[tf]l?[\t ][^\n]*\n[\t ]+add\\.s[\t ]")
    message(FATAL_ERROR
        "a MIPS floating operation that may trap left its delay slot or "
        "passed a trapping comparison\n${trap_order}")
endif()
# A range test is one unsigned comparison under -ftree-reassoc, also
# without -fif-conversion and at -Oz, and a subtraction from zero subtracts
# from $zero.
function_body("${OUTPUT}.enabled.s" mips_range_test range_test)
function_body("${OUTPUT}.no_if_conversion.s" mips_range_test range_no_if)
function_body("${OUTPUT}.size.s" mips_range_test range_size)
function_body("${OUTPUT}.no_reassoc.s" mips_range_test range_branches)
string(REGEX MATCHALL "[\t ]slti?[\t ]" range_compares "${range_branches}")
list(LENGTH range_compares range_compare_count)
foreach(range_body IN ITEMS "${range_test}" "${range_no_if}" "${range_size}")
    if(NOT range_body MATCHES "[\t ]sltu[\t ]" OR
       range_body MATCHES "[\t ]slti?[\t ]")
        message(FATAL_ERROR
            "a MIPS range test was not one unsigned comparison\n"
            "${range_body}")
    endif()
endforeach()
if(NOT range_test MATCHES "[\t ]subu[\t ][^\n]*,\\$zero," OR
   range_compare_count LESS 2)
    message(FATAL_ERROR
        "a MIPS negation did not subtract from $zero, or -fno-tree-reassoc "
        "was ignored\nenabled:\n${range_test}\nwithout:\n${range_branches}")
endif()
# When neither successor of a latch follows it, the branch-likely back edge
# runs the body's first load in its slot, and the exit's jump, after any PHI
# copies, follows that slot.
function_body("${OUTPUT}.unroll_risc.s" mips_rounds_sum rounds_sum)
function_body("${OUTPUT}.unroll_risc.s" mips_rounds_sum64 rounds_sum64)
function_body("${OUTPUT}.unroll_risc_no_delay.s" mips_rounds_sum64
              rounds_sum64_no_delay)
if(NOT rounds_sum MATCHES
       "[\t ]bnel[\t ][^\n]*\n[\t ]+lw[\t ][^\n]*\n[\t ]+b[\t ]" OR
   NOT rounds_sum64 MATCHES
       "[\t ]bnel[\t ][^\n]*\n[\t ]+ld[\t ][^\n]*\n[\t ]+b[\t ][^\n]*\n[\t ]+move[\t ]" OR
   rounds_sum64_no_delay MATCHES "[\t ]bnel[\t ]")
    message(FATAL_ERROR
        "a MIPS latch with both successors out of line left its back "
        "edge's delay slot empty, or -fno-schedule-insns2 was ignored\n"
        "${rounds_sum}\n${rounds_sum64}\nwithout:\n${rounds_sum64_no_delay}")
endif()
# Elements at `row * 4 + k` share a pointer recurrence that steps by a row
# under -fivopts.
function_body("${OUTPUT}.enabled.s" mips_matrix_rows matrix_rows)
function_body("${OUTPUT}.no_ivopts.s" mips_matrix_rows matrix_rows_indexed)
if(NOT matrix_rows MATCHES "[\t ]lw[\t ][^\n]*,12\\(" OR
   NOT matrix_rows MATCHES "[\t ]addiu[\t ][^\n]*,16\n" OR
   matrix_rows_indexed MATCHES "[\t ]addiu[\t ][^\n]*,16\n")
    message(FATAL_ERROR
        "MIPS scaled row accesses did not share a pointer recurrence, or "
        "-fno-ivopts was ignored\n"
        "enabled:\n${matrix_rows}\nwithout:\n${matrix_rows_indexed}")
endif()
# Doubleword constants take GNU as's `dli` sequences, without a zero high
# word.
function_body("${OUTPUT}.enabled.s" mips_wide_masks wide_masks)
if(NOT wide_masks MATCHES
       "[\t ]addiu[\t ]+([$][a-z0-9]+),[$]zero,-1\n[\t ]+dsrl32[\t ]+[$][a-z0-9]+,[$][a-z0-9]+,0\n" OR
   NOT wide_masks MATCHES
       "[\t ]dsrl[\t ][^\n]*,28\n[\t ]+dsll[\t ][^\n]*,24\n" OR
   wide_masks MATCHES "[\t ]li[\t ][^\n]*,0\n")
    message(FATAL_ERROR
        "MIPS doubleword constants took longer sequences\n${wide_masks}")
endif()
# An inner loop whose test heads it tests at its latch, where a pointer
# increment fills the slot, under -freorder-blocks.
function_body("${OUTPUT}.enabled.s" mips_inner_exit_test inner_exit)
function_body("${OUTPUT}.no_reorder.s" mips_inner_exit_test inner_exit_unrotated)
if(NOT inner_exit MATCHES
       "[\t ]beq[\t ][^\n]*\n[\t ]+addiu[\t ]+[$][a-z0-9]+,[$][a-z0-9]+,4\n" OR
   inner_exit_unrotated MATCHES
       "[\t ]beq[\t ][^\n]*\n[\t ]+addiu[\t ]+[$][a-z0-9]+,[$][a-z0-9]+,4\n")
    message(FATAL_ERROR
        "an inner loop did not test at its latch, or -fno-reorder-blocks "
        "was ignored\n"
        "enabled:\n${inner_exit}\nwithout:\n${inner_exit_unrotated}")
endif()
# A branch's delay slot runs the first operation of the block it falls
# through to when the taken edge does not read its result.
function_body("${OUTPUT}.enabled.s" mips_fallthrough_delay fallthrough_delay)
function_body("${OUTPUT}.no_delay.s" mips_fallthrough_delay
              fallthrough_delay_empty)
if(NOT fallthrough_delay MATCHES
       "[\t ]and[\t ][^\n]*\n[\t ]+beq[\t ][^\n]*\n[\t ]+li[\t ][^\n]*,0\n" OR
   NOT fallthrough_delay_empty MATCHES
       "[\t ]and[\t ][^\n]*\n[\t ]+beq[\t ][^\n]*\n[\t ]+nop\n")
    message(FATAL_ERROR
        "a branch did not run its fallthrough's first operation in its "
        "delay slot, or -fno-schedule-insns2 was ignored\n"
        "enabled:\n${fallthrough_delay}\nwithout:\n${fallthrough_delay_empty}")
endif()
# Loop constants live at the loop's pressure peak leave it at -Oz without
# another saved register, and a loop with calls keeps a constant in a
# call-preserved register the function saves anyway.
function_body("${OUTPUT}.size.s" mips_range_loop range_loop)
function_body("${OUTPUT}.size_no_licm.s" mips_range_loop range_loop_inside)
function_body("${OUTPUT}.enabled.s" mips_call_constant_loop call_constants)
function_body("${OUTPUT}.no_licm.s" mips_call_constant_loop
              call_constants_inside)
string(REGEX MATCHALL "[\t ]sd[\t ]+[$]s[0-7]," range_saves "${range_loop}")
string(REGEX MATCHALL "[\t ]sd[\t ]+[$]s[0-7]," call_saves "${call_constants}")
string(REGEX MATCHALL "[\t ]sd[\t ]+[$]s[0-7]," call_saves_inside
       "${call_constants_inside}")
list(LENGTH range_saves range_save_count)
list(LENGTH call_saves call_save_count)
list(LENGTH call_saves_inside call_save_count_inside)
# The hoisted constant comes before the loop's first array access in the
# layout; in the loop it comes after it.
string(FIND "${range_loop}" ",2097152\n" range_constant)
string(FIND "${range_loop}" "%lo(mips_range_px)" range_access)
string(FIND "${range_loop_inside}" ",2097152\n" range_constant_inside)
string(FIND "${range_loop_inside}" "%lo(mips_range_px)" range_access_inside)
if(range_constant EQUAL -1 OR NOT range_constant LESS range_access OR
   range_constant_inside LESS range_access_inside OR
   range_save_count GREATER 1 OR
   call_constants MATCHES "[\t ]li[\t ][^\n]*,24\n[\t ]+beql" OR
   NOT call_constants_inside MATCHES "[\t ]li[\t ][^\n]*,24\n[\t ]+beql" OR
   NOT call_save_count EQUAL call_save_count_inside)
    message(FATAL_ERROR
        "MIPS loop constants at a pressure peak or in a loop with calls "
        "stayed in the loop or added a saved register, or "
        "-fno-move-loop-invariants was ignored\n"
        "size:\n${range_loop}\nwithout:\n${range_loop_inside}\n"
        "calls:\n${call_constants}\nwithout:\n${call_constants_inside}")
endif()
# A truncation that only stores read shares its source's register.
function_body("${OUTPUT}.enabled.s" mips_truncated_store truncated_store)
if(NOT truncated_store MATCHES "[\t ]sh[\t ]" OR
   truncated_store MATCHES "[\t ]move[^\n]*\n[\t ]+sh[\t ]")
    message(FATAL_ERROR
        "a halfword store copied its live source first\n${truncated_store}")
endif()
# Without branch-likely forms (R3000), an ordinary branch runs a one-operation
# arm in its delay slot when its other edge does not read the result.
function_body("${OUTPUT}.r3000.s" mips_ring_step ring_step_r3000)
function_body("${OUTPUT}.no_delay.s" mips_ring_step ring_step_unfilled)
if(NOT ring_step_r3000 MATCHES "[\t ]bne[\t ][^\n]*\n[\t ]+addiu[\t ][^\n]*,-1\n" OR
   ring_step_r3000 MATCHES "[\t ]b[a-z]+l[\t ]" OR
   ring_step_unfilled MATCHES "[\t ]bne[\t ][^\n]*\n[\t ]+addiu[\t ][^\n]*,-1\n")
    message(FATAL_ERROR
        "an R3000 branch did not run its taken arm in its delay slot, or "
        "-fno-schedule-insns2 was ignored\n"
        "r3000:\n${ring_step_r3000}\nwithout:\n${ring_step_unfilled}")
endif()
