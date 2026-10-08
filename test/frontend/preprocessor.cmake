# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CPP SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CPP}" "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp failed (${status})\n${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" source)
foreach(pattern
        "global i32 patch_value_u64 = 1;"
        "global i32 patch_value_u128 = 0;"
        "global i32 patch_operand_movabs = 1;"
        "global i32 patch_operand_add = 0;"
        "global i32 patch_operand_signed = 1;"
        "global i32 patch_operand_float = 0;"
        "global i32 patch_operand_narrow = 0;"
        "global i32 operator_attribute = 1;"
        "global i32 patch_repeatable = 0;"
        "global i32 patch_concurrent = 0;"
        "global i64 language_version = 800i64;"
        "global i32 automatic_evaluation ="
        "global i32 atomic_feature = 1;"
        "global i32 variadic_feature = 1;"
        "global i32 target_avx = 0;"
        "global i32 target_avx2 = 0;"
        "global i32 target_avx512f = 0;"
        "global i32 target_avx512bw = 0;"
        "global i32 atomic_intrinsic ="
        "global i32 atomic_order_builtin ="
        "1;")
    string(FIND "${source}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "preprocessed output is missing '${pattern}'\n${source}")
    endif()
endforeach()

execute_process(
    COMMAND "${CPP}" -mavx512bw "${SOURCE}" -o "${OUTPUT}.extensions"
    RESULT_VARIABLE extension_status
    OUTPUT_VARIABLE extension_stdout
    ERROR_VARIABLE extension_stderr
)
if(NOT extension_status EQUAL 0)
    message(FATAL_ERROR
        "extension-aware cpp failed (${extension_status})\n"
        "${extension_stdout}\n${extension_stderr}")
endif()
file(READ "${OUTPUT}.extensions" extension_source)
foreach(feature avx avx2 avx512f avx512bw)
    string(FIND "${extension_source}"
        "global i32 target_${feature} = 1;" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "preprocessor lost implied target feature '${feature}'\n"
            "${extension_source}")
    endif()
endforeach()
