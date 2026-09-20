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

set(ir_output "${OUTPUT}.ll")
run_cc("${ir_output}" -emit-llvm -O2)
run_cc("${OUTPUT}.windows.o" -c -O2 -target x86_64-w64-windows-gnu)
run_cc("${OUTPUT}.linux.o" -c -O2 -target x86_64-unknown-linux-gnu)

file(READ "${ir_output}" ir)
foreach(pattern
        "Cross language 0.8"
        "global i64 178998315"
        "global i64 33"
        "global i64 44"
        "global i64 55"
        "global fp128 0xL3FFF8000000000000000000000000000"
        "add i128"
        "21267647932558653966460912964485513216"
        "movabsq"
        "movabsq $$1234605616436508552"
        "movl $$1432778632"
        ".Lcross.patch.0.end"
        ".quad .Lcross.patch.0.end-8"
        ".Lcross.patch.value.0.end"
        ".quad .Lcross.patch.value.0.end-8"
        "managed_patch_expression"
        "noinline"
        "section \".text.cross.patch."
        ".text$cross.patch."
        "add i64 %mir.v2, 123"
        "xor i32"
        "raw_inline_entry:"
        "addq"
        "raw_inline_complex:"
        "raw_inline_structured:"
        "raw_inline_memory:"
        "raw_inline_store:"
        "shrq"
        "xorq"
        "imulq")
    string(FIND "${ir}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "semantic expansion IR is missing '${pattern}'")
    endif()
endforeach()

string(FIND "${ir}" "1234605616436508675" folded_patch_position)
if(NOT folded_patch_position EQUAL -1)
    message(FATAL_ERROR "$::patch initial value was constant-folded through +123")
endif()
string(FIND "${ir}" "@\"managed_patch_site\" = global" llvm_sink_position)
if(NOT llvm_sink_position EQUAL -1)
    message(FATAL_ERROR "managed $::patch sink was defined by both LLVM and assembly")
endif()

string(REGEX MATCH "raw_inline_entry:[^\"]*" raw_inline_assembly "${ir}")
if(raw_inline_assembly MATCHES "call")
    message(FATAL_ERROR "raw_inline left a call in the naked function")
endif()
string(REGEX MATCH "raw_inline_complex:[^\"]*" raw_inline_complex_assembly "${ir}")
foreach(register "%r9" "%r10" "%r11" "%r8")
    string(FIND "${raw_inline_complex_assembly}" "${register}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "raw_inline complex lowering did not use declared register ${register}")
    endif()
endforeach()
foreach(symbol raw_inline_structured raw_inline_memory raw_inline_store)
    string(REGEX MATCH "${symbol}:[^\"]*" raw_inline_body "${ir}")
    if(raw_inline_body MATCHES "call")
        message(FATAL_ERROR "${symbol} retained a call after raw-compatible inlining")
    endif()
endforeach()
string(FIND "${ir}" "hash40" eval_function_position)
if(NOT eval_function_position EQUAL -1)
    message(FATAL_ERROR "eval_only function retained a runtime definition")
endif()
string(FIND "${ir}" "identity" identity_position)
string(FIND "${ir}" "add_generated_object" generator_position)
if(NOT identity_position EQUAL -1 OR NOT generator_position EQUAL -1)
    message(FATAL_ERROR "macro function retained a runtime definition")
endif()

execute_process(
    COMMAND "${CC}" --print-features
    RESULT_VARIABLE feature_status
    OUTPUT_VARIABLE features
    ERROR_VARIABLE feature_error
)
if(NOT feature_status EQUAL 0)
    message(FATAL_ERROR "--print-features failed\n${feature_error}")
endif()
foreach(feature integer128 binary128_storage binary128_arithmetic fixed_vectors atomics variadics evaluation automatic_evaluation generics
                procedural_macros patchable_values patchable_operands raw_inline)
    string(FIND "${features}" "$::feature::${feature}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "feature registry is missing ${feature}")
    endif()
endforeach()
