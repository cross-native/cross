# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_extension name)
    execute_process(
        COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    if(NOT assembly_status EQUAL 0)
        message(FATAL_ERROR
            "${name} assembly selection failed (${assembly_status})\n"
            "${assembly_stdout}\n${assembly_stderr}")
    endif()
    execute_process(
        COMMAND "${CC}" -c -O2 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "${name} object selection failed (${object_status})\n"
            "${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}.${name}.s" ${name})
    set(${name} "${${name}}" PARENT_SCOPE)
endfunction()

compile_extension(base -mno-avx)
compile_extension(avx -mavx)
compile_extension(avx2 -mavx2)
compile_extension(avx512f -mavx512f)
compile_extension(avx512bw -mavx512bw)
compile_extension(haswell -march=haswell)
compile_extension(preferred128 -mavx512f -mprefer-vector-width=128)

if(base MATCHES "%[yz]mm" OR base MATCHES "[\t ]v(add|mul|padd|pmul)")
    message(FATAL_ERROR "baseline selection acquired an ISA extension\n${base}")
endif()
if(NOT base MATCHES "pcmpgtd[\t ]+%xmm0, %xmm1" OR
   NOT base MATCHES "pshufd[\t ]+[$]0, %xmm2, %xmm2")
    message(FATAL_ERROR "baseline SSE2 packed comparisons are incomplete\n${base}")
endif()
if(NOT avx MATCHES "vaddps[\t ]+%ymm1, %ymm0, %ymm0" OR
   NOT avx MATCHES "vmulps[\t ]+%ymm1, %ymm0, %ymm0" OR
   NOT avx MATCHES "vxorps[\t ]+%ymm1, %ymm0, %ymm0" OR
   avx MATCHES "vp(add|mul)[a-z]+[\t ]+%ymm" OR
   avx MATCHES "vps(ll|ra|rl)v[a-z]+[\t ]+%ymm")
    message(FATAL_ERROR "AVX floating selection is incomplete or used AVX2\n${avx}")
endif()
foreach(text IN ITEMS avx2 haswell)
    if(NOT "${${text}}" MATCHES "vpaddd[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       NOT "${${text}}" MATCHES "vpmulld[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       NOT "${${text}}" MATCHES "vpsubd[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       NOT "${${text}}" MATCHES "vpcmpgtd[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       NOT "${${text}}" MATCHES "(vpbroadcastd[\t ]+%xmm[0-9]+, %ymm[0-9]+|vpslld[\t ]+[$]31, %ymm[0-9]+, %ymm[0-9]+)" OR
       NOT "${${text}}" MATCHES "vpsravd[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       NOT "${${text}}" MATCHES "vpsrlvd[\t ]+%ymm[0-9]+, %ymm[0-9]+, %ymm[0-9]+" OR
       "${${text}}" MATCHES "%zmm")
        message(FATAL_ERROR "${text} did not select AVX2 YMM arithmetic\n${${text}}")
    endif()
endforeach()
if(NOT avx512f MATCHES "vpaddd[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512f MATCHES "vpmulld[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512f MATCHES "vaddps[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512f MATCHES "vpcmpd[\t ]+[$]4, %zmm[0-9]+, %zmm[0-9]+, %k1" OR
   NOT avx512f MATCHES "vpcmpud[\t ]+[$]1, %zmm[0-9]+, %zmm[0-9]+, %k1" OR
   NOT avx512f MATCHES "vcmpps[\t ]+[$]2, %zmm[0-9]+, %zmm[0-9]+, %k1" OR
   NOT avx512f MATCHES "vpbroadcastd[\t ]+%eax, %zmm0 .*%k1" OR
   NOT avx512f MATCHES "vpsllvd[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512f MATCHES "vpternlogd[\t ]+[$]0x55, %zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512f MATCHES "vxorps[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+" OR
   avx512f MATCHES "vpbroadcastw[\t ]+%xmm0, %zmm")
    message(FATAL_ERROR "AVX-512F selection is incomplete or used BW\n${avx512f}")
endif()
if(NOT avx512bw MATCHES "vpbroadcastw[\t ]+%xmm[0-9]+, %zmm[0-9]+" OR
   NOT avx512bw MATCHES "vpsllvw[\t ]+%zmm[0-9]+, %zmm[0-9]+, %zmm[0-9]+")
    message(FATAL_ERROR "AVX-512BW did not select a word ZMM broadcast\n${avx512bw}")
endif()
if(NOT preferred128 MATCHES "vpaddd[\t ]+%xmm[0-9]+, %xmm[0-9]+, %xmm[0-9]+" OR
   NOT preferred128 MATCHES "vaddps[\t ]+%xmm[0-9]+, %xmm[0-9]+, %xmm[0-9]+" OR
   preferred128 MATCHES "v(paddd|addps)[\t ]+%zmm")
    message(FATAL_ERROR
        "-mprefer-vector-width=128 did not limit internal operations\n${preferred128}")
endif()

execute_process(
    COMMAND "${CC}" -S -mavx512f -mno-avx2 "${SOURCE}"
            -o "${OUTPUT}.conflict.s"
    RESULT_VARIABLE conflict_status
    OUTPUT_VARIABLE conflict_stdout
    ERROR_VARIABLE conflict_stderr
)
if(conflict_status EQUAL 0 OR
   NOT conflict_stderr MATCHES "requires '-mavx2'")
    message(FATAL_ERROR
        "contradictory extension selection lacks a dependency error\n"
        "${conflict_stdout}\n${conflict_stderr}")
endif()

execute_process(
    COMMAND "${CC}" --print-features -mavx512bw
    RESULT_VARIABLE feature_status
    OUTPUT_VARIABLE features
    ERROR_VARIABLE feature_stderr
)
if(NOT feature_status EQUAL 0)
    message(FATAL_ERROR "--print-features failed\n${feature_stderr}")
endif()
foreach(feature avx avx2 avx512f avx512bw)
    if(NOT features MATCHES
       "[$]::feature::${feature} target extension \\[enabled\\]")
        message(FATAL_ERROR
            "--print-features lost enabled ${feature}\n${features}")
    endif()
endforeach()
