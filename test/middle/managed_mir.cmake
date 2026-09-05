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
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cc failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

run_cc("${OUTPUT}.O0.ll" -emit-llvm -O0)
run_cc("${OUTPUT}.O2.ll" -emit-llvm -O2)
run_cc("${OUTPUT}.O2.o" -c -O2)
run_cc("${OUTPUT}.linux.O2.o" -c -O2 -target x86_64-unknown-linux-gnu)

file(READ "${OUTPUT}.O0.ll" ir)
file(READ "${OUTPUT}.O2.ll" optimized_ir)

function(function_body result symbol)
    string(REGEX MATCH "define[^\n]*@\"${symbol}\"\\([^}]*}" body "${ir}")
    if(body STREQUAL "")
        message(FATAL_ERROR "LLVM IR lacks ${symbol}")
    endif()
    set(${result} "${body}" PARENT_SCOPE)
endfunction()

function_body(choose_ir "mir_choose")
function_body(if_ir "mir_if")
function_body(safe_div_ir "mir_safe_div")
function_body(widen_ir "mir_widen")
function_body(small_rank_ir "mir_small_rank")
function_body(wide_rank_ir "mir_wide_rank")
function_body(effect_loop_ir "mir_effect_loop")
function_body(copyout_ir "mir_copyout_call")
function_body(entry_ir "managed_mir_entry")

foreach(name choose if safe_div widen small_rank wide_rank entry)
    if(${name}_ir MATCHES "alloca")
        message(FATAL_ERROR "${name} unexpectedly fell back to stack-based AST lowering")
    endif()
endforeach()

if(NOT choose_ir MATCHES "phi i32")
    message(FATAL_ERROR "conditional expression did not produce an SSA phi")
endif()
if(NOT effect_loop_ir MATCHES "%mir[.]slot" OR
   NOT effect_loop_ir MATCHES "call i32.*mir_choose" OR
   NOT effect_loop_ir MATCHES "br label %mir[.]bb")
    message(FATAL_ERROR "effectful loop did not lower through managed MIR")
endif()
if(NOT effect_loop_ir MATCHES "lifetime[.]start" OR
   NOT effect_loop_ir MATCHES "lifetime[.]end")
    message(FATAL_ERROR "local object lifetime markers are missing")
endif()
if(NOT copyout_ir MATCHES "%mir[.]slot" OR
   NOT copyout_ir MATCHES "call void.*mir_copyout" OR
   NOT copyout_ir MATCHES "store i32.*%mir[.]slot0")
    message(FATAL_ERROR "result-bearing call did not lower through managed MIR copy-out")
endif()
string(REGEX MATCHALL "ret i32" if_returns "${if_ir}")
list(LENGTH if_returns return_count)
if(NOT return_count EQUAL 2)
    message(FATAL_ERROR "fully terminating if lost a return path")
endif()
if(NOT safe_div_ir MATCHES "phi i8" OR NOT safe_div_ir MATCHES "sdiv i32")
    message(FATAL_ERROR "short-circuit division did not produce the expected MIR CFG")
endif()
if(NOT widen_ir MATCHES "sext i32" OR NOT widen_ir MATCHES "phi i64")
    message(FATAL_ERROR "mixed-width conditional lost its predecessor-local conversion")
endif()
if(NOT small_rank_ir MATCHES "icmp slt i32")
    message(FATAL_ERROR "small integer operands did not promote to signed i32")
endif()
if(NOT wide_rank_ir MATCHES "icmp slt i64")
    message(FATAL_ERROR "i64 did not retain signedness when combined with u32")
endif()
if(NOT entry_ir MATCHES "call i32.*mir_choose" OR
   NOT entry_ir MATCHES "call i32.*mir_safe_div")
    message(FATAL_ERROR "managed MIR lost direct all-in calls")
endif()
if(NOT optimized_ir MATCHES
       "call i64 @llvm[.]fshl[.]i64\\(i64 [^,]+, i64 [^,]+, i64 [^)]+\\)" OR
   NOT optimized_ir MATCHES
       "declare i64 @llvm[.]fshl[.]i64\\(i64, i64, i64\\)")
    message(FATAL_ERROR
        "MIR rotate did not serialize through LLVM's funnel shift intrinsic")
endif()
