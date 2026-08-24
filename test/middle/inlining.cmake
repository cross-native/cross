# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_ir suffix)
    execute_process(
        COMMAND "${CC}" -emit-llvm ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${suffix}.ll"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "inlining compile ${suffix} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

compile_ir(O2 -O2)
compile_ir(no-inline -O0 -fno-inline)
compile_ir(explicit -O0 -finline-functions)

file(READ "${OUTPUT}.O2.ll" optimized)
file(READ "${OUTPUT}.no-inline.ll" disabled)
file(READ "${OUTPUT}.explicit.ll" explicit)

string(REGEX MATCH "define i64 @\"inline_entry\"\\([^}]*}"
       optimized_body "${optimized}")
string(REGEX MATCH "define i64 @\"inline_entry\"\\([^}]*}"
       disabled_body "${disabled}")
string(REGEX MATCH "define i64 @\"inline_entry\"\\([^}]*}"
       explicit_body "${explicit}")
if(optimized_body STREQUAL "" OR disabled_body STREQUAL "" OR
   explicit_body STREQUAL "")
    message(FATAL_ERROR "one inlining output lacks inline_entry")
endif()

if(optimized_body MATCHES "inline_add" OR
   optimized_body MATCHES "mandatory_double" OR
   NOT optimized_body MATCHES "retained_add")
    message(FATAL_ERROR
        "O2 did not apply inline > noinline policy\n${optimized_body}")
endif()

if(NOT disabled_body MATCHES "inline_add" OR
   disabled_body MATCHES "mandatory_double" OR
   NOT disabled_body MATCHES "retained_add")
    message(FATAL_ERROR
        "-fno-inline did not retain ordinary calls or lost mandatory inline\n${disabled_body}")
endif()

if(explicit_body MATCHES "inline_add" OR
   explicit_body MATCHES "mandatory_double" OR
   NOT explicit_body MATCHES "retained_add")
    message(FATAL_ERROR
        "explicit O0 inlining did not enable the pass\n${explicit_body}")
endif()

execute_process(
    COMMAND "${CC}" -c -O2 "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE native_status
    OUTPUT_VARIABLE native_stdout
    ERROR_VARIABLE native_stderr
)
if(NOT native_status EQUAL 0)
    message(FATAL_ERROR
        "native inlining output failed\n${native_stdout}\n${native_stderr}")
endif()
