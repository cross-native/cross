# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE QUERY_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(features
    -madx -mrdrnd -mrdseed -msha -mgfni -mvpclmulqdq -mvaes -mserialize
    -mavx512vnni -mavx512vbmi -mavx512vbmi2
    -mavx512bitalg -mavx512vpopcntdq)

execute_process(
    COMMAND "${CC}" -S ${features} "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
execute_process(
    COMMAND "${CC}" -c ${features} "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "modern ISA forms failed\n${assembly_stdout}\n${assembly_stderr}\n"
        "${object_stdout}\n${object_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
foreach(opcode adcxq adoxq rdrandq rdseedq sha1rnds4 sha1msg1
               sha1msg2 sha256msg1 sha256msg2 gf2p8mulb vpclmulqdq vaesenc
               vpdpbusd vpermb vpshldvd vpopcntb vpopcntq)
    if(NOT assembly MATCHES "[\t ]${opcode}[\t ]")
        message(FATAL_ERROR "modern ISA output is missing ${opcode}\n${assembly}")
    endif()
endforeach()
string(FIND "${assembly}" "\tserialize" serialize_position)
if(serialize_position EQUAL -1)
    message(FATAL_ERROR "modern ISA output is missing serialize\n${assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S ${features} -mno-avx512vnni "${SOURCE}"
            -o "${OUTPUT}.vnni-off.s"
    RESULT_VARIABLE reject_status
    OUTPUT_VARIABLE reject_stdout
    ERROR_VARIABLE reject_stderr
)
if(reject_status EQUAL 0 OR NOT reject_stderr MATCHES
   "target instruction '[$]::_vpdpbusd512' requires feature 'avx512vnni'")
    message(FATAL_ERROR
        "AVX-512 VNNI feature gate was not enforced\n"
        "${reject_stdout}\n${reject_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -mvpclmulqdq -mno-pclmul "${SOURCE}"
            -o "${OUTPUT}.dependency.s"
    RESULT_VARIABLE dependency_status
    OUTPUT_VARIABLE dependency_stdout
    ERROR_VARIABLE dependency_stderr
)
if(dependency_status EQUAL 0 OR NOT dependency_stderr MATCHES
   "requires '-mpclmul'.*'-mno-pclmul'")
    message(FATAL_ERROR
        "compound target-feature dependency was not diagnosed\n"
        "${dependency_stdout}\n${dependency_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -E ${features} "${QUERY_SOURCE}"
            -o "${OUTPUT}.query-on.i"
    RESULT_VARIABLE query_on_status
    OUTPUT_VARIABLE query_on_stdout
    ERROR_VARIABLE query_on_stderr
)
execute_process(
    COMMAND "${CC}" -E ${features} -mno-avx512vbmi2 "${QUERY_SOURCE}"
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
   NOT query_on MATCHES "has_adx[\t ]*=[\t ]*1" OR
   NOT query_on MATCHES "has_vector_crypto[\t ]*=[\t ]*1" OR
   NOT query_on MATCHES "has_vbmi2[\t ]*=[\t ]*1" OR
   NOT query_off MATCHES "has_vbmi2[\t ]*=[\t ]*0")
    message(FATAL_ERROR
        "modern instruction queries are incorrect\n"
        "enabled:\n${query_on}\n${query_on_stdout}\n${query_on_stderr}\n"
        "disabled:\n${query_off}\n${query_off_stdout}\n${query_off_stderr}")
endif()
