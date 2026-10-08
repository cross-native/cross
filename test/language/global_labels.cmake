# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE NOP_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O0 -target x86_64-unknown-linux-gnu
            "${NOP_SOURCE}" -o "${OUTPUT}-nop.s"
    RESULT_VARIABLE nop_status
    OUTPUT_VARIABLE nop_stdout
    ERROR_VARIABLE nop_stderr
)
if(NOT nop_status EQUAL 0)
    message(FATAL_ERROR
        "global-label managed-nop probe failed\n${nop_stdout}\n${nop_stderr}")
endif()
file(READ "${OUTPUT}-nop.s" nop_assembly)
if(NOT nop_assembly MATCHES "global_label_nop_owner::resume" OR
   NOT nop_assembly MATCHES "[\t ]nop([\t\r\n ]|$)")
    message(FATAL_ERROR
        "global-label managed-nop output is incomplete\n${nop_assembly}")
endif()
execute_process(
    COMMAND "${CC}" -c -O0 -target x86_64-unknown-linux-gnu
            "${NOP_SOURCE}" -o "${OUTPUT}-nop.o"
    RESULT_VARIABLE nop_object_status
    OUTPUT_VARIABLE nop_object_stdout
    ERROR_VARIABLE nop_object_stderr
)
if(NOT nop_object_status EQUAL 0)
    message(FATAL_ERROR
        "global-label managed-nop object assembly failed\n"
        "${nop_object_stdout}\n${nop_object_stderr}")
endif()

function(compile output target)
    execute_process(
        COMMAND "${CC}" -S -O0 -fno-eval-calls -target "${target}"
                "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "global-label compilation failed for ${target}\n${stdout}\n${stderr}")
    endif()
endfunction()

compile("${OUTPUT}-elf.s" x86_64-unknown-linux-gnu)
compile("${OUTPUT}-coff.s" x86_64-w64-windows-gnu)
compile("${OUTPUT}-mips.s" mips64-unknown-elf)

foreach(target_output
        "elf;x86_64-unknown-linux-gnu"
        "coff;x86_64-w64-windows-gnu"
        "mips;mips64-unknown-elf")
    list(GET target_output 0 kind)
    list(GET target_output 1 target)
    execute_process(
        COMMAND "${CC}" -c -O0 -fno-eval-calls -target "${target}"
                "${SOURCE}" -o "${OUTPUT}-${kind}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "global-label object assembly failed for ${target}\n${object_stdout}\n${object_stderr}")
    endif()
endforeach()

foreach(kind elf coff mips)
    file(READ "${OUTPUT}-${kind}.s" assembly)
    foreach(pattern
            "cross_global_resume:"
            "cross_global_pointer"
            "cross_global_resume"
            "cross_external_pointer"
            "external_label_owner::resume")
        string(FIND "${assembly}" "${pattern}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR
                "${kind} global-label output is missing '${pattern}'")
        endif()
    endforeach()
endforeach()

file(READ "${OUTPUT}-elf.s" elf)
foreach(pattern
        ".globl cross_global_resume"
        ".type cross_global_resume,@function"
        ".quad cross_global_resume")
    string(FIND "${elf}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "ELF global-label output is missing '${pattern}'")
    endif()
endforeach()
if(NOT elf MATCHES "[.]globl [^\r\n]*default_label_owner[^\r\n]*resume")
    message(FATAL_ERROR "default mangling did not export the global label")
endif()

file(READ "${OUTPUT}-coff.s" coff)
string(FIND "${coff}" ".def cross_global_resume; .scl 2; .type 32; .endef" position)
if(position EQUAL -1)
    message(FATAL_ERROR "COFF global label lacks an external code-symbol definition")
endif()

set(label_errors
        "a global label definition requires a global stable-ABI function"
        "global label declarations use different link names"
        "global label declaration disagrees with a local label definition"
        "global label declaration has no matching definition"
        "a function containing a global label cannot be always_inline")
# Source constraints may stop before HIR/linkage checks. Exercise each invalid
# declaration independently so an earlier diagnostic does not hide coverage.
foreach(case RANGE 0 4)
    list(GET label_errors ${case} pattern)
    execute_process(
        COMMAND "${CC}" -S "-DLABEL_ERROR_CASE=${case}"
                "${ERROR_SOURCE}" -o "${OUTPUT}-errors-${case}.s"
        RESULT_VARIABLE error_status
        OUTPUT_VARIABLE error_stdout
        ERROR_VARIABLE error_stderr
    )
    if(NOT error_status EQUAL 1 OR NOT error_stderr MATCHES "${pattern}" OR
       NOT error_stderr MATCHES "global_label_errors.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR
            "global-label case ${case} is missing '${pattern}'\n${error_stdout}\n${error_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}.ll"
    RESULT_VARIABLE llvm_status
    OUTPUT_VARIABLE llvm_stdout
    ERROR_VARIABLE llvm_stderr
)
if(llvm_status EQUAL 0 OR
   NOT llvm_stderr MATCHES
       "LLVM debug serialization cannot define an externally named global label")
    message(FATAL_ERROR
        "LLVM global-label boundary was not diagnosed\n${llvm_stdout}\n${llvm_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-gimple "${SOURCE}" -o "${OUTPUT}.gimple.c"
    RESULT_VARIABLE gimple_status
    OUTPUT_VARIABLE gimple_stdout
    ERROR_VARIABLE gimple_stderr
)
if(gimple_status EQUAL 0 OR
   NOT gimple_stderr MATCHES
       "GIMPLE serialization cannot define an externally named global label")
    message(FATAL_ERROR
        "GIMPLE global-label boundary was not diagnosed\n${gimple_stdout}\n${gimple_stderr}")
endif()
