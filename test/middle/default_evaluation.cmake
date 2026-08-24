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

run_cc("${OUTPUT}.default.ll" -emit-llvm -O0)
run_cc("${OUTPUT}.disabled.ll" -emit-llvm -O0 -fno-eval-calls)

file(READ "${OUTPUT}.default.ll" default_ir)
file(READ "${OUTPUT}.disabled.ll" disabled_ir)

foreach(ir default_ir disabled_ir)
    if(NOT "${${ir}}" MATCHES
       "@\"required_global\" = global i64 42")
        message(FATAL_ERROR
            "required static initializer was not evaluated in ${ir}")
    endif()
    if(NOT "${${ir}}" MATCHES
       "@\"required_short_circuit\" = global i32 1" OR
       NOT "${${ir}}" MATCHES
       "@\"required_compound_once\" = global i32 3")
        message(FATAL_ERROR
            "required sequencing evaluation was not preserved in ${ir}")
    endif()
    if("${${ir}}" MATCHES "(\\$::eval|\\$::runtime|evaluated_add)")
        message(FATAL_ERROR
            "staging syntax or evaluation-only definition survived in ${ir}")
    endif()
    if(NOT "${${ir}}" MATCHES "call i64.*runtime_add" OR
       NOT "${${ir}}" MATCHES "call i64.*runtime_seed")
        message(FATAL_ERROR
            "runtime-only calls were evaluated in ${ir}")
    endif()
endforeach()

string(REGEX MATCHALL "call i64 @\"__cross_static_[0-9]+_add\"" default_add_calls "${default_ir}")
list(LENGTH default_add_calls default_add_count)
if(NOT default_add_count EQUAL 2)
    message(FATAL_ERROR
        "default evaluation retained ${default_add_count} add calls, expected 2")
endif()

string(REGEX MATCHALL "call i64 @\"__cross_static_[0-9]+_add\"" disabled_add_calls "${disabled_ir}")
list(LENGTH disabled_add_calls disabled_add_count)
if(NOT disabled_add_count EQUAL 3)
    message(FATAL_ERROR
        "-fno-eval-calls retained ${disabled_add_count} add calls, expected 3")
endif()
