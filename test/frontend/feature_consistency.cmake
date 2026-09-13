# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(preprocess label source)
    execute_process(
        COMMAND "${CC}" -E ${ARGN} "${SOURCE_DIR}/${source}"
                -o "${OUTPUT}-${label}.x"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} feature query failed\n${stdout}\n${stderr}")
    endif()
endfunction()

preprocess(x86 x86_64_features.x)
preprocess(mips mips_features.x -target mips64-unknown-elf -mno-llsc)
preprocess(mips-llsc mips_llsc_features.x -mprofile=vr4300-o32 -mllsc)

execute_process(
    COMMAND "${CC}" --print-features -target mips64-unknown-elf -mno-llsc
    RESULT_VARIABLE status OUTPUT_VARIABLE features ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "MIPS --print-features failed\n${stderr}")
endif()
foreach(feature integer128 binary128_storage binary128_arithmetic fixed_vectors
                atomics variadics thread_local)
    if(features MATCHES "[$]::feature::${feature}([\r\n]|$)")
        message(FATAL_ERROR
            "MIPS --print-features over-reports ${feature}\n${features}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" --print-features
    RESULT_VARIABLE status OUTPUT_VARIABLE features ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR
   NOT features MATCHES "[$]::feature::thread_local([\r\n]|$)")
    message(FATAL_ERROR
        "x86-64 --print-features omits thread_local\n${features}\n${stderr}")
endif()
