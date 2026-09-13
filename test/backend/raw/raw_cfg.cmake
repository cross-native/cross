# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

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

run_cc("${OUTPUT}-linux.ll" -emit-llvm -target x86_64-unknown-linux-gnu)
run_cc("${OUTPUT}-linux.gimple.c" -emit-gimple -target x86_64-unknown-linux-gnu)
run_cc("${OUTPUT}-linux.s" -S -O0 -target x86_64-unknown-linux-gnu)
run_cc("${OUTPUT}-linux.o" -c -O2 -target x86_64-unknown-linux-gnu)
run_cc("${OUTPUT}-windows.s" -S -O2 -target x86_64-w64-windows-gnu)
run_cc("${OUTPUT}-windows.o" -c -O2 -target x86_64-w64-windows-gnu)

foreach(suffix linux.s windows.s)
    file(READ "${OUTPUT}-${suffix}" assembly)
    foreach(pattern "raw_cfg:" "raw_goto:" "raw_computed_goto:"
                    "raw_goto_done:" ".globl raw_goto_done"
                    "raw_structured:" "je" "jne" "ja" "jb" "jl" "jmp"
                    "*%rax" "pushq" "popq" "retq" ".Lcross.")
        string(FIND "${assembly}" "${pattern}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${suffix} lacks raw CFG form ${pattern}")
        endif()
    endforeach()
endforeach()

file(READ "${OUTPUT}-linux.gimple.c" gimple)
foreach(pattern "raw_goto_done:" ".globl raw_goto_done")
    string(FIND "${gimple}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "GIMPLE raw CFG output lacks ${pattern}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_error
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_error}")
endif()
foreach(instruction "$::_jmp" "$::_jmp_indirect" "$::_je" "$::_jne"
                    "$::_ja" "$::_jae" "$::_jb" "$::_jbe"
                    "$::_jg" "$::_jge" "$::_jl" "$::_jle")
    string(FIND "${registry}" "${instruction} instruction" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "registry lacks ${instruction}")
    endif()
endforeach()
