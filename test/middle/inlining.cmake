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

# The calls inline_entry keeps; every other helper is inlined.
function(check_calls suffix)
    file(READ "${OUTPUT}.${suffix}.ll" ir)
    string(REGEX MATCH "define i64 @\"inline_entry\"\\([^}]*}" body "${ir}")
    if(body STREQUAL "")
        message(FATAL_ERROR "${suffix} output lacks inline_entry")
    endif()
    foreach(callee inline_add retained_add mandatory_double inline_magnitude
                   retained_magnitude mandatory_clamp)
        list(FIND ARGN ${callee} kept)
        if(body MATCHES "call [^\n]*${callee}")
            if(kept EQUAL -1)
                message(FATAL_ERROR "${suffix} did not inline ${callee}\n${body}")
            endif()
        elseif(NOT kept EQUAL -1)
            message(FATAL_ERROR "${suffix} inlined ${callee}\n${body}")
        endif()
    endforeach()
endfunction()

compile_ir(O2 -O2)
compile_ir(no-inline -O0 -fno-inline)
compile_ir(O2-no-inline -O2 -fno-inline-functions -fno-inline-functions-called-once)
compile_ir(explicit -O0 -finline-functions)

check_calls(O2 retained_add retained_magnitude)
check_calls(no-inline inline_add retained_add inline_magnitude
            retained_magnitude)
check_calls(O2-no-inline inline_add retained_add inline_magnitude
            retained_magnitude)
check_calls(explicit retained_add retained_magnitude)

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
