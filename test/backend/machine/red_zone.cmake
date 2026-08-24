# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly suffix red_zone variable)
    execute_process(
        COMMAND "${CC}" -S -O2 -fno-eval-calls -fomit-frame-pointer
                -target x86_64-unknown-linux-gnu -mabi=sysv_abi
                "-m${red_zone}red-zone"
                "${SOURCE}" -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${suffix} red-zone compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" assembly)
    set(${variable} "${assembly}" PARENT_SCOPE)
endfunction()

function(extract_function assembly symbol variable)
    string(FIND "${assembly}" "${symbol}:" start)
    string(FIND "${assembly}" ".size ${symbol}," end)
    if(start EQUAL -1 OR end EQUAL -1 OR NOT end GREATER start)
        message(FATAL_ERROR "could not find assembly body for ${symbol}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

compile_assembly(enabled "" enabled_assembly)
compile_assembly(disabled "no-" disabled_assembly)

extract_function("${enabled_assembly}" red_zone_leaf enabled_leaf)
if(enabled_leaf MATCHES "(subq|pushq)[^\r\n]*%rsp")
    message(FATAL_ERROR
        "eligible leaf allocated a stack frame with the red zone enabled\n"
        "${enabled_leaf}")
endif()

extract_function("${disabled_assembly}" red_zone_leaf disabled_leaf)
if(enabled_leaf MATCHES "-[0-9]+\\(%rsp\\)")
    if(NOT disabled_leaf MATCHES "subq[\t ]+\\$[0-9]+,[\t ]+%rsp" OR
       disabled_leaf MATCHES "-[0-9]+\\(%rsp\\)")
        message(FATAL_ERROR
            "-mno-red-zone did not allocate the leaf frame\n${disabled_leaf}")
    endif()
elseif(disabled_leaf MATCHES "(subq|pushq)[^\r\n]*%rsp")
    message(FATAL_ERROR
        "register-only leaf changed stack shape under -mno-red-zone\n"
        "${disabled_leaf}")
endif()

extract_function("${enabled_assembly}" red_zone_nonleaf enabled_nonleaf)
if(NOT enabled_nonleaf MATCHES "callq?[\t ]+[^\r\n]*red_zone_callee" OR
   NOT enabled_nonleaf MATCHES "subq[\t ]+\\$[0-9]+,[\t ]+%rsp")
    message(FATAL_ERROR
        "a non-leaf function incorrectly used the red zone\n${enabled_nonleaf}")
endif()

extract_function("${enabled_assembly}" red_zone_stack_input stack_input)
if(stack_input MATCHES "-[0-9]+\\(%rsp\\)" OR
   NOT stack_input MATCHES "[0-9]+\\(%rsp\\)")
    message(FATAL_ERROR
        "an incoming stack argument was addressed through the red zone\n"
        "${stack_input}")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -fomit-frame-pointer
            -target x86_64-pc-windows-msvc -mred-zone
            "${SOURCE}" -o "${OUTPUT}.coff.s"
    RESULT_VARIABLE coff_status
    OUTPUT_VARIABLE coff_stdout
    ERROR_VARIABLE coff_stderr
)
if(coff_status EQUAL 0)
    message(FATAL_ERROR "COFF unexpectedly accepted -mred-zone")
endif()
if(NOT coff_stderr MATCHES
       "option '-mred-zone' is unavailable for the x86-64 COFF platform contract")
    message(FATAL_ERROR
        "COFF red-zone diagnostic was not actionable\n"
        "${coff_stdout}\n${coff_stderr}")
endif()
