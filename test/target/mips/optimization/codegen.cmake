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
compile_variant(no_rotation -fno-tree-loop-rotate)
compile_variant(grouped_select -fif-conversion-limit=12)
compile_variant(grouped_select_no_fusion -fif-conversion-limit=12
                -fno-compare-select-fusion)
compile_variant(no_delay -fno-schedule-insns2)
compile_variant(no_schedule -fno-schedule-insns)
compile_variant(no_machine_combine -fno-machine-combine)
compile_variant(no_slsr -fno-tree-slsr)
compile_variant(no_ccp -fno-tree-ccp)
compile_variant(no_licm -fno-move-loop-invariants)
compile_variant(cisc -mrisc-cisc-balance=100)
compile_variant(fix4300 -mfix4300)
compile_variant(fix4300_no_schedule -mfix4300 -fno-schedule-insns)
compile_variant(r4000 -march=r4000 -mtune=r4000)
compile_variant(no_cprop -fno-cprop-registers)
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
compile_unroll_variant(unroll_risc_no_iv 0 -fno-ivopts)
compile_unroll_variant(unroll_risc_no_reorder 0 -fno-reorder-blocks
                       -fno-tree-loop-rotate)
compile_unroll_variant(unroll_risc_no_reorder_no_delay 0
                       -fno-reorder-blocks -fno-schedule-insns2
                       -fno-tree-loop-rotate)
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

function_body("${OUTPUT}.enabled.s" mips_shared_epilogue shared_epilogue)
if(shared_epilogue MATCHES
       "[	 ]b[	 ]+\\.Lcross[.]mips[.][0-9]+[.]return" OR
   NOT shared_epilogue MATCHES
       "\\.Lcross[.]mips[.][0-9]+[.]return:\n" OR
   NOT shared_epilogue MATCHES "[.]cfi_restore")
    message(FATAL_ERROR
        "a final return should fall through to its CFI epilogue\n"
        "${shared_epilogue}")
endif()
function_body("${OUTPUT}.enabled.s" mips_multiple_epilogues multiple_epilogues)
string(REGEX MATCHALL
       "[	 ]b[	 ]+\\.Lcross[.]mips[.][0-9]+[.]return"
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

function_body("${OUTPUT}.enabled.s"
              mips_successor_delay scheduled_successor)
function_body("${OUTPUT}.no_delay.s"
              mips_successor_delay empty_successor)
if(NOT scheduled_successor MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9]+[.]tmp[.][0-9]+\n[\t ]+xori[\t ]" OR
   NOT scheduled_successor MATCHES
       "[\t ]xori[^\n]*\n\\.Lcross[.]mips[.][0-9]+[.]tmp[.][0-9]+:\n[\t ]+addiu[\t ]" OR
   NOT empty_successor MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9]+[.]bb[.][0-9]+\n[\t ]+nop")
    message(FATAL_ERROR
        "an unconditional edge did not execute a safe successor operation "
        "in its delay slot or -fno-schedule-insns2 was ignored\n"
        "scheduled:\n${scheduled_successor}\ndisabled:\n${empty_successor}")
endif()

function_body("${OUTPUT}.enabled.s"
              mips_reassociation_pressure scheduled_successor_compare)
function_body("${OUTPUT}.no_delay.s"
              mips_reassociation_pressure empty_successor_compare)
if(NOT scheduled_successor_compare MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9]+[.]tmp[.][0-9]+\n[\t ]+sltu[\t ]" OR
   NOT scheduled_successor_compare MATCHES
       "[\t ]sltu[^\n]*\n\\.Lcross[.]mips[.][0-9]+[.]tmp[.][0-9]+:\n[\t ]+b(eq|ne)z?l?[\t ]" OR
   NOT empty_successor_compare MATCHES
       "[\t ]b[\t ]+\\.Lcross[.]mips[.][0-9]+[.]bb[.][0-9]+\n[\t ]+nop")
    message(FATAL_ERROR
        "a fused successor comparison did not fill an unconditional edge "
        "delay slot or -fno-schedule-insns2 was ignored\n"
        "scheduled:\n${scheduled_successor_compare}\n"
        "disabled:\n${empty_successor_compare}")
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

function_body("${OUTPUT}.enabled.s"
              mips_signed_constant_multiply signed_multiply)
if(NOT signed_multiply MATCHES "[\t ]dmult[\t ]" OR
   NOT signed_multiply MATCHES "[\t ]mflo[\t ]")
    message(FATAL_ERROR
        "MIR strength reduction changed signed-overflow semantics\n"
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
