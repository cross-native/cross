# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE QUERY_SOURCE CLASS_ERROR_SOURCE FEATURE_ERROR_SOURCE
                 K0_ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -march=skylake-avx512 "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
execute_process(
    COMMAND "${CC}" -c -march=skylake-avx512 "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "SIMD ISA registry forms failed\n${assembly_stdout}\n${assembly_stderr}\n"
        "${object_stdout}\n${object_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
foreach(opcode
        aesenc aesenclast aesdec aesdeclast aesimc aeskeygenassist
        pclmulqdq vcvtps2ph vcvtph2ps vfmadd132ps vpmullq vpconflictd vpaddb
        kandw korw kxorw knotw kortestw kmovw vaddps vpaddd vpcmpd vcmpps
        kandnw kxnorw kaddw kunpckbw kshiftlw kshiftrw ktestw)
    if(NOT assembly MATCHES "[\t ]${opcode}[\t ]")
        message(FATAL_ERROR "raw SIMD output is missing ${opcode}\n${assembly}")
    endif()
endforeach()

foreach(decorator "{%k1}" "{z}" "{1to16}" "{rn-sae}")
    string(FIND "${assembly}" "${decorator}" decorator_position)
    if(decorator_position EQUAL -1)
        message(FATAL_ERROR
            "raw SIMD output is missing EVEX decorator ${decorator}\n${assembly}")
    endif()
endforeach()

function(reject_feature name flag instruction feature)
    execute_process(
        COMMAND "${CC}" -S -march=skylake-avx512 "${flag}" "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0 OR
       NOT stderr MATCHES
           "target instruction '[$]::${instruction}' requires feature '${feature}'")
        message(FATAL_ERROR
            "${name} feature gate was not enforced\n${stdout}\n${stderr}")
    endif()
endfunction()

reject_feature(aes -mno-aes _aesenc aes)
reject_feature(pclmul -mno-pclmul _pclmulqdq pclmul)
reject_feature(f16c -mno-f16c _vcvtps2ph128 f16c)
reject_feature(fma -mno-fma _vfmadd132ps128 fma)
reject_feature(avx512dq -mno-avx512dq _vpmullq512 avx512dq)
reject_feature(avx512cd -mno-avx512cd _vpconflictd512 avx512cd)
reject_feature(avx512bw -mno-avx512bw _vpaddb512 avx512bw)
reject_feature(avx512vl -mno-avx512vl _vpmullq256 avx512vl)

execute_process(
    COMMAND "${CC}" -S -mavx512f "${FEATURE_ERROR_SOURCE}"
            -o "${OUTPUT}.opmask-on.s"
    RESULT_VARIABLE opmask_on_status
    OUTPUT_VARIABLE opmask_on_stdout
    ERROR_VARIABLE opmask_on_stderr
)
execute_process(
    COMMAND "${CC}" -S -mno-avx512f "${FEATURE_ERROR_SOURCE}"
            -o "${OUTPUT}.opmask-off.s"
    RESULT_VARIABLE opmask_off_status
    OUTPUT_VARIABLE opmask_off_stdout
    ERROR_VARIABLE opmask_off_stderr
)
if(NOT opmask_on_status EQUAL 0 OR opmask_off_status EQUAL 0 OR
   NOT opmask_off_stderr MATCHES
       "target instruction '[$]::_knotw' requires feature 'avx512f'")
    message(FATAL_ERROR
        "opmask feature gate was not enforced\n"
        "enabled:\n${opmask_on_stdout}\n${opmask_on_stderr}\n"
        "disabled:\n${opmask_off_stdout}\n${opmask_off_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -mavx512f "${CLASS_ERROR_SOURCE}"
            -o "${OUTPUT}.opmask-class.s"
    RESULT_VARIABLE class_status
    OUTPUT_VARIABLE class_stdout
    ERROR_VARIABLE class_stderr
)
if(class_status EQUAL 0 OR
   NOT class_stderr MATCHES
       "instruction operand requires target register class 'mask'")
    message(FATAL_ERROR
        "instruction register-class checking was not enforced\n"
        "${class_stdout}\n${class_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -march=skylake-avx512 "${K0_ERROR_SOURCE}"
            -o "${OUTPUT}.opmask-k0.s"
    RESULT_VARIABLE k0_status
    OUTPUT_VARIABLE k0_stdout
    ERROR_VARIABLE k0_stderr
)
if(k0_status EQUAL 0 OR
   NOT k0_stderr MATCHES "explicit EVEX writemask must use k1-k7")
    message(FATAL_ERROR
        "explicit k0 writemask was not rejected\n${k0_stdout}\n${k0_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -E -march=skylake-avx512 "${QUERY_SOURCE}"
            -o "${OUTPUT}.query-on.i"
    RESULT_VARIABLE query_on_status
    OUTPUT_VARIABLE query_on_stdout
    ERROR_VARIABLE query_on_stderr
)
execute_process(
    COMMAND "${CC}" -E -march=skylake-avx512 -mno-avx512vl "${QUERY_SOURCE}"
            -o "${OUTPUT}.query-off.i"
    RESULT_VARIABLE query_off_status
    OUTPUT_VARIABLE query_off_stdout
    ERROR_VARIABLE query_off_stderr
)
if(query_on_status EQUAL 0)
    file(READ "${OUTPUT}.query-on.i" query_on)
endif()
if(query_off_status EQUAL 0)
    file(READ "${OUTPUT}.query-off.i" query_off)
endif()
if(NOT query_on_status EQUAL 0 OR NOT query_off_status EQUAL 0 OR
   NOT query_on MATCHES "has_avx512dq_vl[\t ]*=[\t ]*1" OR
   NOT query_off MATCHES "has_avx512dq[\t ]*=[\t ]*1" OR
   NOT query_off MATCHES "has_avx512dq_vl[\t ]*=[\t ]*0")
    message(FATAL_ERROR
        "multi-feature instruction queries are incorrect\n"
        "enabled:\n${query_on}\n${query_on_stdout}\n${query_on_stderr}\n"
        "VL disabled:\n${query_off}\n${query_off_stdout}\n${query_off_stderr}")
endif()

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
string(FIND "${registry}"
       "$::_vpmullq256 instruction [avx512dq,avx512vl]"
       compound_instruction_position)
if(NOT registry_status EQUAL 0 OR compound_instruction_position EQUAL -1)
    message(FATAL_ERROR
        "instruction registry omitted the compound feature predicate\n"
        "${registry}\n${registry_stderr}")
endif()
