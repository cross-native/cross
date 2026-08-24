# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC CPP MODEL MODEL_PATH ABI_DSL INVALID_ABI INVALID_MANGLER
        SOURCE COMPOSITION_SOURCE ATTRIBUTE_ERROR UNKNOWN_ATTRIBUTE_ERROR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CPP}" "--model=${MODEL}" -mprofile=test-profile
            "${SOURCE}" -o "${OUTPUT}.i"
    RESULT_VARIABLE cpp_status
    OUTPUT_VARIABLE cpp_output
    ERROR_VARIABLE cpp_error
)
if(NOT cpp_status EQUAL 0)
    message(FATAL_ERROR "cpp did not load the selected models\n${cpp_error}")
endif()
file(READ "${OUTPUT}.i" preprocessed)
if(NOT preprocessed MATCHES
   "[$]::static_assert\\(1, \"custom ABI model is visible\"\\)")
    message(FATAL_ERROR
        "cpp and cc model environments disagree\n${preprocessed}")
endif()

execute_process(
    COMMAND "${CC}" "--model-path=${MODEL_PATH}" --model=path.xm
            --print-manglings
    RESULT_VARIABLE path_status
    OUTPUT_VARIABLE path_output
    ERROR_VARIABLE path_error
)
if(NOT path_status EQUAL 0 OR NOT path_output MATCHES "path-test")
    message(FATAL_ERROR
        "model search path was not loaded\n${path_error}\n${path_output}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -mprofile=test-profile -S
            "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "custom model compilation failed\n${compile_error}")
endif()
file(READ "${OUTPUT}" assembly)
if(NOT assembly MATCHES "_ZT11model_entry")
    message(FATAL_ERROR
        "custom mangling model was not applied\n${assembly}")
endif()
if(NOT assembly MATCHES "_ZT14model_identityG1_t3i32")
    message(FATAL_ERROR
        "generic symbols did not execute the custom mangler DSL\n${assembly}")
endif()
if(NOT assembly MATCHES "_ZT11model_countG1_v57uptr")
    message(FATAL_ERROR
        "generic value arguments did not execute their mangler DSL branch\n${assembly}")
endif()
if(NOT assembly MATCHES "%r10d" OR
   NOT assembly MATCHES "%r8d" OR
   NOT assembly MATCHES "call[\t ]+_ZT14model_odd_echo")
    message(FATAL_ERROR
        "native backend did not execute custom ABI register banks\n${assembly}")
endif()
if(NOT assembly MATCHES "\\.text\\._ZT11model_entry")
    message(FATAL_ERROR
        "profile function_sections default was not applied\n${assembly}")
endif()

# ABI placement and symbol spelling are independent model axes. Exercise the
# same mangler with two implementation ABIs whose first integer register
# differs on x86-64.
execute_process(
    COMMAND "${CC}" "--model=${MODEL}" "--model=${ABI_DSL}"
            -target x86_64-unknown-linux-gnu
            -mabi=go_internal -mmangling=test -S
            "${COMPOSITION_SOURCE}" -o "${OUTPUT}.go-mangling.s"
    RESULT_VARIABLE go_mangling_status
    OUTPUT_VARIABLE go_mangling_output
    ERROR_VARIABLE go_mangling_error
)
if(NOT go_mangling_status EQUAL 0)
    message(FATAL_ERROR
        "Go ABI/mangler composition failed\n${go_mangling_output}\n${go_mangling_error}")
endif()
file(READ "${OUTPUT}.go-mangling.s" go_mangling_assembly)
if(NOT go_mangling_assembly MATCHES "_ZT16composition_echo" OR
   NOT go_mangling_assembly MATCHES "movl[\t ]+%eax")
    message(FATAL_ERROR
        "Go ABI and selected mangler did not compose\n${go_mangling_assembly}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" "--model=${ABI_DSL}"
            -target x86_64-unknown-linux-gnu
            -mabi=rust_native -mmangling=test -S
            "${COMPOSITION_SOURCE}" -o "${OUTPUT}.rust-mangling.s"
    RESULT_VARIABLE rust_mangling_status
    OUTPUT_VARIABLE rust_mangling_output
    ERROR_VARIABLE rust_mangling_error
)
if(NOT rust_mangling_status EQUAL 0)
    message(FATAL_ERROR
        "Rust ABI/mangler composition failed\n${rust_mangling_output}\n${rust_mangling_error}")
endif()
file(READ "${OUTPUT}.rust-mangling.s" rust_mangling_assembly)
if(NOT rust_mangling_assembly MATCHES "_ZT16composition_echo" OR
   NOT rust_mangling_assembly MATCHES "movl[\t ]+%edi")
    message(FATAL_ERROR
        "Rust ABI and selected mangler did not compose\n${rust_mangling_assembly}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -mmangling=descriptor -S
            "${SOURCE}" -o "${OUTPUT}.descriptor.s"
    RESULT_VARIABLE descriptor_status
    OUTPUT_VARIABLE descriptor_output
    ERROR_VARIABLE descriptor_error
)
if(NOT descriptor_status EQUAL 0)
    message(FATAL_ERROR
        "descriptor-aware mangler compilation failed\n${descriptor_error}")
endif()
file(READ "${OUTPUT}.descriptor.s" descriptor_assembly)
if(NOT descriptor_assembly MATCHES
   "F_MODEL_ENTRY_i32_1_b_N_M")
    message(FATAL_ERROR
        "entity descriptors were not available to the mangler\n${descriptor_assembly}")
endif()
if(NOT descriptor_assembly MATCHES
   "F_MODEL_FUNCTION_ABI_i32_2_12_N_M_I0of1_i32")
    message(FATAL_ERROR
        "parameter descriptors were not available to the mangler\n${descriptor_assembly}")
endif()
if(NOT descriptor_assembly MATCHES
   "O_MODEL_DATA_i32_1_a_N_M")
    message(FATAL_ERROR
        "object descriptors were not available to the mangler\n${descriptor_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -fno-function-sections -mprofile=test-profile
            "--model=${MODEL}" -S "${SOURCE}" -o "${OUTPUT}.override.s"
    RESULT_VARIABLE override_status
    OUTPUT_VARIABLE override_output
    ERROR_VARIABLE override_error
)
if(NOT override_status EQUAL 0)
    message(FATAL_ERROR "profile override compilation failed\n${override_error}")
endif()
file(READ "${OUTPUT}.override.s" override_assembly)
if(override_assembly MATCHES "\\.text\\._ZT11model_entry")
    message(FATAL_ERROR
        "profile overrode an explicit -fno-function-sections choice\n${override_assembly}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" --print-abis
            -target x86_64-unknown-linux-gnu
    RESULT_VARIABLE print_status
    OUTPUT_VARIABLE abi_output
    ERROR_VARIABLE abi_error
)
if(NOT print_status EQUAL 0 OR NOT abi_output MATCHES "test_sysv")
    message(FATAL_ERROR "custom ABI model is not visible\n${abi_error}\n${abi_output}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" --print-optimizations
    RESULT_VARIABLE optimization_list_status
    OUTPUT_VARIABLE optimization_list
    ERROR_VARIABLE optimization_list_error
)
if(NOT optimization_list_status EQUAL 0 OR
   NOT optimization_list MATCHES "test-opt  inherits: O2" OR
   NOT optimization_list MATCHES "test-target-opt  inherits: test-opt")
    message(FATAL_ERROR
        "custom optimization entries were not listed\n${optimization_list_error}\n${optimization_list}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -mprofile=test-option-profile
            -O3 --print-options=all
    RESULT_VARIABLE option_profile_status
    OUTPUT_VARIABLE option_profile_output
    ERROR_VARIABLE option_profile_error
)
if(NOT option_profile_status EQUAL 0 OR
   NOT option_profile_output MATCHES
       "f.tree-ccp  type: boolean  value: off  origin: profile" OR
   NOT option_profile_output MATCHES
       "f.inline-limit  type: unsigned\\[0\\.\\.1048576\\]  value: 256  origin: preset" OR
   NOT option_profile_output MATCHES
       "m.arch  type: text  value: haswell  origin: profile" OR
   NOT option_profile_output MATCHES
       "m.tune  type: text  value: skylake  origin: profile" OR
   NOT option_profile_output MATCHES
       "m.avx  type: boolean  value: on  origin: profile")
    message(FATAL_ERROR
        "profile/preset typed-option precedence is incorrect\n${option_profile_error}\n${option_profile_output}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -O=test-target-opt
            --print-options=target
    RESULT_VARIABLE target_preset_status
    OUTPUT_VARIABLE target_preset_output
    ERROR_VARIABLE target_preset_error
)
if(NOT target_preset_status EQUAL 0 OR
   NOT target_preset_output MATCHES
       "m.tune  type: text  value: znver3  origin: preset")
    message(FATAL_ERROR
        "target-constrained optimization option was not resolved\n${target_preset_error}\n${target_preset_output}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -mprofile=test-option-profile
            -ftree-ccp --print-options=optimization
    RESULT_VARIABLE direct_option_status
    OUTPUT_VARIABLE direct_option_output
    ERROR_VARIABLE direct_option_error
)
if(NOT direct_option_status EQUAL 0 OR
   NOT direct_option_output MATCHES
       "f.tree-ccp  type: boolean  value: on  origin: command-line")
    message(FATAL_ERROR
        "direct option did not override the profile\n${direct_option_error}\n${direct_option_output}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -emit-llvm "${ATTRIBUTE_ERROR}"
            -o "${OUTPUT}.ll"
    RESULT_VARIABLE attribute_status
    OUTPUT_VARIABLE attribute_output
    ERROR_VARIABLE attribute_error
)
if(attribute_status EQUAL 0 OR
   NOT attribute_error MATCHES "attributes are contextual names; omit the '[$]::' prefix")
    message(FATAL_ERROR
        "old rooted attribute did not receive its migration diagnostic\n${attribute_error}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -emit-llvm
            "${UNKNOWN_ATTRIBUTE_ERROR}" -o "${OUTPUT}.unknown.ll"
    RESULT_VARIABLE unknown_status
    OUTPUT_VARIABLE unknown_output
    ERROR_VARIABLE unknown_error
)
if(unknown_status EQUAL 0 OR
   NOT unknown_error MATCHES "unknown attribute 'not_a_cross_attribute'")
    message(FATAL_ERROR
        "unknown contextual attribute was not rejected\n${unknown_error}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${MODEL}.missing" --print-models
    RESULT_VARIABLE missing_status
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(missing_status EQUAL 0 OR
   NOT missing_error MATCHES "cannot find Cross model file")
    message(FATAL_ERROR
        "an explicitly missing model did not fail\n${missing_error}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${ABI_DSL}" --print-abis
            -target x86_64-unknown-linux-gnu
    RESULT_VARIABLE abi_dsl_status
    OUTPUT_VARIABLE abi_dsl_output
    ERROR_VARIABLE abi_dsl_error
)
if(NOT abi_dsl_status EQUAL 0 OR
   NOT abi_dsl_output MATCHES "go_internal" OR
   NOT abi_dsl_output MATCHES "rust_native")
    message(FATAL_ERROR
        "general ABI rule models were not accepted\n${abi_dsl_error}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${INVALID_ABI}" --print-models
    RESULT_VARIABLE invalid_abi_status
    OUTPUT_VARIABLE invalid_abi_output
    ERROR_VARIABLE invalid_abi_error
)
if(invalid_abi_status EQUAL 0 OR
   NOT invalid_abi_error MATCHES "names unknown bank 'missing'")
    message(FATAL_ERROR
        "invalid ABI rule silently fell back\n${invalid_abi_error}")
endif()

execute_process(
    COMMAND "${CC}" "--model=${INVALID_MANGLER}" --print-models
    RESULT_VARIABLE mangler_status
    OUTPUT_VARIABLE mangler_output
    ERROR_VARIABLE mangler_error
)
if(mangler_status EQUAL 0 OR
   NOT mangler_error MATCHES "unknown mangler helper 'mystery'")
    message(FATAL_ERROR
        "invalid mangler DSL was not diagnosed\n${mangler_error}")
endif()
