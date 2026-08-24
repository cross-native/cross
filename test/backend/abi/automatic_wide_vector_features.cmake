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
            "${suffix} registered-vector compilation failed\n${error}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" ${suffix}_assembly)
    set(${suffix}_assembly "${${suffix}_assembly}" PARENT_SCOPE)
endfunction()

compile_assembly(base -mno-avx)
compile_assembly(avx -mavx)
compile_assembly(avx512 -mavx512f)

if(base_assembly MATCHES "%ymm[0-9]" OR
   base_assembly MATCHES "%zmm[0-9]")
    message(FATAL_ERROR
        "baseline registered-vector ABI unexpectedly requires AVX\n"
        "${base_assembly}")
endif()
if(NOT base_assembly MATCHES "andq[\t ]+\\$-64,[\t ]+%(rsp|r11)")
    message(FATAL_ERROR
        "baseline 512-bit stack arguments did not realign the caller frame\n"
        "${base_assembly}")
endif()

if(NOT avx_assembly MATCHES "vmovdqu[\t ]+%ymm0" OR
   NOT avx_assembly MATCHES "%ymm1" OR
   avx_assembly MATCHES "%zmm[0-9]")
    message(FATAL_ERROR
        "AVX registered-vector ABI did not select YMM endpoints\n"
        "${avx_assembly}")
endif()

if(NOT avx512_assembly MATCHES "vmovdqu64[\t ]+%zmm0" OR
   NOT avx512_assembly MATCHES "vmovdqu64[\t ]+[^\n]*%zmm0")
    message(FATAL_ERROR
        "AVX-512 registered-vector ABI did not select ZMM endpoints\n"
        "${avx512_assembly}")
endif()

foreach(feature avx avx512f)
    execute_process(
        COMMAND "${CC}" -c -O0 -m${feature} "${SOURCE}"
                -o "${OUTPUT}.${feature}.o"
        RESULT_VARIABLE status
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "assembler rejected ${feature} registered-vector output\n"
            "${error}")
    endif()
endforeach()
