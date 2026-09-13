# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE PARSER_ERRORS LOWERING_ERRORS STACK_ERROR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(compile_tail kind target)
    execute_process(
        COMMAND "${CC}" -S -O0 -fno-optimize-sibling-calls
                -fno-eval-calls -target "${target}" ${ARGN} "${SOURCE}"
                -o "${OUTPUT}-${kind}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "musttail compilation failed for ${target}\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${kind}.s" assembly)
    if(kind MATCHES "^x86")
        if(NOT assembly MATCHES "jmp[q]?[	 ]+[^\r\n]*musttail_callee" OR
           NOT assembly MATCHES "jmp[q]?[	 ]+[^\r\n]*musttail_void_callee" OR
           assembly MATCHES "call[q]?[	 ]+[^\r\n]*musttail_callee" OR
           assembly MATCHES "call[q]?[	 ]+[^\r\n]*musttail_void_callee")
            message(FATAL_ERROR
                "${target} did not preserve mandatory x86 tail transfers\n${assembly}")
        endif()
    else()
        if(NOT assembly MATCHES "[	 ]j[	 ]+musttail_callee" OR
           NOT assembly MATCHES "[	 ]j[	 ]+musttail_void_callee" OR
           assembly MATCHES "[	 ]jal[	 ]+musttail_callee" OR
           assembly MATCHES "[	 ]jal[	 ]+musttail_void_callee")
            message(FATAL_ERROR
                "${target} did not preserve mandatory MIPS tail transfers\n${assembly}")
        endif()
    endif()
    execute_process(
        COMMAND "${CC}" -c -O0 -fno-optimize-sibling-calls
                -fno-eval-calls -target "${target}" ${ARGN} "${SOURCE}"
                -o "${OUTPUT}-${kind}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "musttail object assembly failed for ${target} ${ARGN}\n"
            "${object_stdout}\n${object_stderr}")
    endif()
endfunction()

compile_tail(x86-elf x86_64-unknown-linux-gnu)
compile_tail(x86-coff x86_64-w64-windows-gnu)
compile_tail(mips-be mips-unknown-elf)
compile_tail(mips-le mipsel-unknown-elf)
compile_tail(vr4300-be mips-unknown-elf -mabi=o32 -march=vr4300)
compile_tail(vr4300-le mipsel-unknown-elf -mabi=o32 -march=vr4300)
compile_tail(allegrex mipsallegrexel-sony-psp-elf)
compile_tail(mips64-be mips64-unknown-elf)
compile_tail(mips64-le mips64el-unknown-elf)

execute_process(
    COMMAND "${CC}" -emit-llvm -O0 -fno-optimize-sibling-calls
            -fno-eval-calls -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.ll"
    RESULT_VARIABLE llvm_status
    OUTPUT_VARIABLE llvm_stdout
    ERROR_VARIABLE llvm_stderr
)
if(NOT llvm_status EQUAL 0)
    message(FATAL_ERROR
        "musttail LLVM serialization failed\n${llvm_stdout}\n${llvm_stderr}")
endif()
file(READ "${OUTPUT}.ll" llvm)
foreach(callee musttail_callee musttail_void_callee)
    if(NOT llvm MATCHES "musttail call [^\r\n]*${callee}")
        message(FATAL_ERROR "LLVM output lacks musttail call to ${callee}\n${llvm}")
    endif()
endforeach()
find_program(LLVM_AS NAMES llvm-as)
if(LLVM_AS)
    execute_process(
        COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
        RESULT_VARIABLE verify_status
        OUTPUT_VARIABLE verify_stdout
        ERROR_VARIABLE verify_stderr
    )
    if(NOT verify_status EQUAL 0)
        message(FATAL_ERROR
            "LLVM rejected musttail debug IR\n${verify_stdout}\n${verify_stderr}")
    endif()
else()
    message(STATUS "skipping optional musttail LLVM verification: llvm-as unavailable")
endif()

execute_process(
    COMMAND "${CC}" -emit-gimple -O0 -fno-eval-calls "${SOURCE}"
            -o "${OUTPUT}.gimple.c"
    RESULT_VARIABLE gimple_status
    OUTPUT_VARIABLE gimple_stdout
    ERROR_VARIABLE gimple_stderr
)
if(gimple_status EQUAL 0 OR NOT gimple_stderr MATCHES
   "GCC GIMPLE serialization cannot guarantee a Cross musttail transfer")
    message(FATAL_ERROR
        "GIMPLE musttail boundary was not diagnosed\n${gimple_stdout}\n${gimple_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S "${PARSER_ERRORS}" -o "${OUTPUT}-parser-errors.s"
    RESULT_VARIABLE parser_status
    OUTPUT_VARIABLE parser_stdout
    ERROR_VARIABLE parser_stderr
)
if(parser_status EQUAL 0)
    message(FATAL_ERROR "invalid musttail syntax unexpectedly compiled")
endif()
foreach(pattern
        "musttail does not take arguments"
        "attribute 'musttail' is not valid on this statement"
        "a return statement has at most one musttail attribute")
    string(FIND "${parser_stderr}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "musttail parser diagnostics lack '${pattern}'\n${parser_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${LOWERING_ERRORS}"
            -o "${OUTPUT}-lowering-errors.s"
    RESULT_VARIABLE lowering_status
    OUTPUT_VARIABLE lowering_stdout
    ERROR_VARIABLE lowering_stderr
)
if(lowering_status EQUAL 0)
    message(FATAL_ERROR "invalid musttail returns unexpectedly compiled")
endif()
foreach(pattern
        "musttail requires returning a call expression directly"
        "musttail output-parameter forwarding is not implemented"
        "musttail cannot restore variable-length array storage"
        "musttail requires the returned value to be the direct result of the call")
    string(FIND "${lowering_stderr}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "musttail lowering diagnostics lack '${pattern}'\n${lowering_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O0 -fno-optimize-sibling-calls
            -target x86_64-unknown-linux-gnu "${STACK_ERROR}"
            -o "${OUTPUT}-stack-error.s"
    RESULT_VARIABLE stack_status
    OUTPUT_VARIABLE stack_stdout
    ERROR_VARIABLE stack_stderr
)
if(stack_status EQUAL 0 OR NOT stack_stderr MATCHES
   "x86-64 cannot satisfy musttail: a tail argument is not a direct scalar register value")
    message(FATAL_ERROR
        "x86 stack musttail boundary was not diagnosed\n${stack_stdout}\n${stack_stderr}")
endif()
