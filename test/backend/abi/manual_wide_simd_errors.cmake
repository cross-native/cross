# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}.base.ll"
    RESULT_VARIABLE base_status
    OUTPUT_VARIABLE base_stdout
    ERROR_VARIABLE base_stderr
)
execute_process(
    COMMAND "${CC}" -emit-llvm -mavx "${SOURCE}" -o "${OUTPUT}.avx.ll"
    RESULT_VARIABLE avx_status
    OUTPUT_VARIABLE avx_stdout
    ERROR_VARIABLE avx_stderr
)
if(base_status EQUAL 0 OR avx_status EQUAL 0)
    message(FATAL_ERROR "unavailable or preserved wide SIMD endpoints were accepted")
endif()

set(stderr "${base_stderr}\n${avx_stderr}")
foreach(expected
        "register endpoint 'ymm3' requires target feature 'avx'"
        "register endpoint 'zmm20' requires target feature 'avx512f'"
        "manual output register 'zmm6' is preserved by the selected ABI")
    if(NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "missing diagnostic: ${expected}\n${base_stdout}\n${avx_stdout}\n${stderr}")
    endif()
endforeach()
