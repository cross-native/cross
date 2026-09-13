# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
if(NOT assembly_status EQUAL 0)
    message(FATAL_ERROR
        "native control-intrinsic compilation failed (${assembly_status})\n"
        "${assembly_stdout}\n${assembly_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S "${ERROR_SOURCE}" -o "${OUTPUT}.errors.s"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr
)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid control intrinsics unexpectedly compiled")
endif()
foreach(pattern
        "assume condition must be side-effect-free"
        "expect requires an integer constant expectation"
        "unreachable takes no arguments"
        "$::_nop takes no arguments")
    string(FIND "${error_stderr}" "${pattern}" error_position)
    if(error_position EQUAL -1)
        message(FATAL_ERROR
            "control-intrinsic diagnostic lacks '${pattern}'\n${error_stderr}")
    endif()
endforeach()

file(READ "${OUTPUT}.s" assembly)
string(REGEX MATCHALL "[\t ]ud2([\t\r\n ]|$)" traps "${assembly}")
list(LENGTH traps trap_count)
if(NOT trap_count EQUAL 1)
    message(FATAL_ERROR
        "trap must emit one ud2 while unreachable emits none\n${assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -target mips-unknown-elf "${SOURCE}"
            -o "${OUTPUT}-mips.s"
    RESULT_VARIABLE mips_status
    OUTPUT_VARIABLE mips_stdout
    ERROR_VARIABLE mips_stderr
)
if(mips_status EQUAL 0 OR NOT mips_stderr MATCHES
   "target instruction '\\$::_nop' is not available")
    message(FATAL_ERROR
        "MIPS accepted unavailable managed $::_nop\n${mips_stdout}\n${mips_stderr}")
endif()

if(GIMPLE_TEXT)
    execute_process(
        COMMAND "${CC}" -emit-gimple "${SOURCE}" -o "${OUTPUT}.gimple.c"
        RESULT_VARIABLE gimple_status
        OUTPUT_VARIABLE gimple_stdout
        ERROR_VARIABLE gimple_stderr
    )
    if(gimple_status EQUAL 0 OR NOT gimple_stderr MATCHES
       "GCC GIMPLE serialization cannot preserve managed \\$::_nop")
        message(FATAL_ERROR
            "GIMPLE accepted managed $::_nop\n${gimple_stdout}\n${gimple_stderr}")
    endif()
endif()
if(assembly MATCHES "call[a-z]*[\t ]+[^\n]*(expect|assume|trap|unreachable)")
    message(FATAL_ERROR
        "control intrinsic introduced a runtime call\n${assembly}")
endif()
if(NOT assembly MATCHES "[\t ]nop([\t\r\n ]|$)")
    message(FATAL_ERROR "managed $::_nop was not emitted\n${assembly}")
endif()

if(LLVM_TEXT)
    execute_process(
        COMMAND "${CC}" -emit-llvm -O2 "${SOURCE}" -o "${OUTPUT}.ll"
        RESULT_VARIABLE llvm_status
        OUTPUT_VARIABLE llvm_stdout
        ERROR_VARIABLE llvm_stderr
    )
    if(NOT llvm_status EQUAL 0)
        message(FATAL_ERROR
            "LLVM debug serialization failed (${llvm_status})\n"
            "${llvm_stdout}\n${llvm_stderr}")
    endif()
    file(READ "${OUTPUT}.ll" llvm)
    foreach(pattern
            "call i32 @llvm.expect.i32"
            "cross.assume (unevaluated)"
            "call void @llvm.trap()"
            "call void asm sideeffect \"nop\", \"\"()"
            "declare i32 @llvm.expect.i32(i32, i32)"
            "declare void @llvm.trap()")
        string(FIND "${llvm}" "${pattern}" pattern_position)
        if(pattern_position EQUAL -1)
            message(FATAL_ERROR
                "LLVM debug form lacks '${pattern}'\n${llvm}")
        endif()
    endforeach()
    execute_process(
        COMMAND llvm-as "${OUTPUT}.ll" -o "${OUTPUT}.bc"
        RESULT_VARIABLE verify_status
        OUTPUT_VARIABLE verify_stdout
        ERROR_VARIABLE verify_stderr
    )
    if(NOT verify_status EQUAL 0)
        message(FATAL_ERROR
            "LLVM rejected control-intrinsic debug IR (${verify_status})\n"
            "${verify_stdout}\n${verify_stderr}")
    endif()
endif()
