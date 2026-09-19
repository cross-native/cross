# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
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

set(linux_assembly "${OUTPUT}-linux.s")
set(windows_assembly "${OUTPUT}-windows.s")
run_cc("${linux_assembly}" -S -target x86_64-unknown-linux-gnu)
run_cc("${windows_assembly}" -S -target x86_64-w64-windows-gnu)
run_cc("${OUTPUT}-linux.o" -c -target x86_64-unknown-linux-gnu)
run_cc("${OUTPUT}-windows.o" -c -target x86_64-w64-windows-gnu)

file(READ "${linux_assembly}" linux)
foreach(pattern
        ".long 4294967254"
        ".quad 11072869122414935808"
        ".quad 1234605616436508552"
        ".long 3217031168"
        ".quad 13832806255468478464"
        ".quad 13835058055282163712"
        ".word 49151"
        ".section \".cross.data\",\"aw\",@progbits"
        ".p2align 5"
        ".section \".data.retained_object\",\"awR\",@progbits"
        ".section \".noinit\",\"aw\",@nobits"
        ".p2align 6"
        ".quad addressed_object"
        ".quad addressed_function")
    string(FIND "${linux}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "native Data IR output is missing '${pattern}'")
    endif()
endforeach()
if(NOT linux MATCHES "\\.quad \\.Lcross\\.label\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "native Data IR output is missing a typed label relocation")
endif()

execute_process(
    COMMAND "${CC}" -S "${ERROR_SOURCE}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr
)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid static Data IR input unexpectedly compiled")
endif()
foreach(pattern
        "noinit object cannot be const"
        "noinit object cannot have an initializer"
        "used requires an object definition"
        "retain requires an object definition")
    string(FIND "${error_stderr}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "static Data IR diagnostics are missing '${pattern}'\n${error_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${ALIGN_ERROR_SOURCE}"
            -o "${OUTPUT}-alignment-error.s"
    RESULT_VARIABLE alignment_status
    OUTPUT_VARIABLE alignment_stdout
    ERROR_VARIABLE alignment_stderr)
if(alignment_status EQUAL 0 OR NOT alignment_stderr MATCHES
   "aligned argument must be a positive power-of-two integer constant")
    message(FATAL_ERROR
        "static object alignment diagnostic missing\n${alignment_stdout}\n${alignment_stderr}")
endif()

file(READ "${windows_assembly}" windows)
foreach(pattern
        ".section \".noinit\",\"bw\""
        ".ascii \" -include:retained_object\"")
    string(FIND "${windows}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "COFF Data IR output is missing '${pattern}'")
    endif()
endforeach()

if(LLVM_TEXT)
    set(llvm_ir "${OUTPUT}.ll")
    run_cc("${llvm_ir}" -emit-llvm)
    file(READ "${llvm_ir}" llvm)
    foreach(pattern
            "global i32 4294967254, align 4"
            "global i128 22774453838368691933757882222884355840"
            "global float 0xBFF8000000000000"
            "global double 0xBFF8000000000000"
            "global x86_fp80 0xKBFFFC000000000000000"
            "global fp128 0xLBFFF8000000000000000000000000000"
            "global ptr @\"addressed_object\""
            "global ptr @\"addressed_function\""
            "blockaddress(@\"dispatch\", %cross.label.target)"
            "section \".cross.data\", align 32"
            "global i64 undef, section \".noinit\", align 64"
            "@llvm.compiler.used = appending global [1 x ptr]"
            "@llvm.used = appending global [1 x ptr]")
        string(FIND "${llvm}" "${pattern}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "LLVM Data IR output is missing '${pattern}'")
        endif()
    endforeach()
endif()
