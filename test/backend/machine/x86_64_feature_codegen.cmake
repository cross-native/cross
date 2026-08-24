# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly suffix)
    execute_process(
        COMMAND "${CC}" -S -O0 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${suffix} feature compilation failed\n${error}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" ${suffix}_assembly)
    set(${suffix}_assembly "${${suffix}_assembly}" PARENT_SCOPE)
endfunction()

compile_assembly(legacy -mno-avx)
compile_assembly(avx -mavx)
compile_assembly(haswell -march=haswell)
compile_assembly(override -march=haswell -mno-avx)

if(NOT legacy_assembly MATCHES "[	 ]addsd[	 ]" OR
   NOT legacy_assembly MATCHES "[	 ]mulsd[	 ]" OR
   NOT legacy_assembly MATCHES "[	 ]ucomisd[	 ]" OR
   NOT legacy_assembly MATCHES "[	 ]cvtsd2ss[	 ]" OR
   legacy_assembly MATCHES "[	 ]vaddsd[	 ]")
    message(FATAL_ERROR
        "legacy scalar floating selection is incomplete\n${legacy_assembly}")
endif()

if(NOT avx_assembly MATCHES "[	 ]vaddsd[	 ]" OR
   NOT avx_assembly MATCHES "[	 ]vmulsd[	 ]" OR
   NOT avx_assembly MATCHES "[	 ]vucomisd[	 ]" OR
   NOT avx_assembly MATCHES "[	 ]vcvtsd2ss[	 ]")
    message(FATAL_ERROR
        "-mavx did not select VEX scalar floating forms\n${avx_assembly}")
endif()

if(NOT haswell_assembly MATCHES "[	 ]vaddsd[	 ]" OR
   override_assembly MATCHES "[	 ]vaddsd[	 ]" OR
   NOT override_assembly MATCHES "[	 ]addsd[	 ]")
    message(FATAL_ERROR
        "architecture baseline/explicit feature precedence did not reach "
        "instruction selection")
endif()

execute_process(
    COMMAND "${CC}" -c -O0 -mavx "${SOURCE}" -o "${OUTPUT}.avx.o"
    RESULT_VARIABLE object_status
    ERROR_VARIABLE object_error
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "assembler rejected AVX target output\n${object_error}")
endif()

if(LLVM_TEXT)
    execute_process(
        COMMAND "${CC}" -emit-llvm -O0 -mavx -ffinite-math-only
                -fno-signed-zeros "${SOURCE}" -o "${OUTPUT}.features.ll"
        RESULT_VARIABLE llvm_status
        ERROR_VARIABLE llvm_error
    )
    if(NOT llvm_status EQUAL 0)
        message(FATAL_ERROR
            "LLVM debug serialization failed\n${llvm_error}")
    endif()
    file(READ "${OUTPUT}.features.ll" llvm_ir)
    if(NOT llvm_ir MATCHES
           "fadd nnan ninf nsz double" OR
       NOT llvm_ir MATCHES
           "\"target-features\"=\"[^\"]*[+]avx" OR
       NOT llvm_ir MATCHES
           "[+]avx,-f16c,-fma,-avx2" OR
       NOT llvm_ir MATCHES
           "-avx512f,-avx512dq,-avx512cd,-avx512bw,-avx512vl")
        message(FATAL_ERROR
            "LLVM debug output lost resolved floating/target options\n${llvm_ir}")
    endif()
endif()
