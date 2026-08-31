cmake_minimum_required(VERSION 3.20)
project(cross VERSION 0.1.0 LANGUAGES CXX)

option(CROSS_ENABLE_LLVM_TEXT
       "Build the optional LLVM text debug/compatibility serializer" ON)
option(CROSS_ENABLE_GCC_GIMPLE_TEXT
       "Build the optional GCC GIMPLE debug/backend-validation serializer" ON)
option(CROSS_STATIC_TOOLS
       "Link the cc and cpp tools without shared compiler runtimes" ON)
set(CROSS_PPSSPP_HEADLESS "" CACHE FILEPATH
    "Optional PPSSPPHeadless executable for Allegrex runtime tests")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CROSS_STATIC_TOOLS AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(CROSS_STATIC_TOOLS AND MINGW AND
   CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    find_program(CROSS_LLD_LINKER NAMES ld.lld)
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build")
endif()

function(cross_component target)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
endfunction()

function(cross_tool target)
    if(NOT CROSS_STATIC_TOOLS OR MSVC)
        return()
    endif()
    if(MINGW)
        # A MinGW-built release must run without libgcc/libstdc++ DLLs or an
        # installed MSYS2/MinGW environment. Windows system DLLs remain normal
        # dynamic imports.
        target_link_options(${target} PRIVATE
            -static -static-libgcc -static-libstdc++
        )
        if(CROSS_LLD_LINKER)
            # GNU ld warns about Clang/libstdc++ COMDATs in a fully static
            # MinGW link. LLD understands both producers and emits a smaller,
            # warning-free PE image.
            target_link_options(${target} PRIVATE -fuse-ld=lld)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Linux distributors may opt out when their sysroot omits static libc.
        target_link_options(${target} PRIVATE -static)
    endif()
endfunction()

add_library(cross_common
    src/common/diagnostic.cpp
    src/common/floating_bits.cpp
    src/common/options.cpp
    src/common/source.cpp
)
cross_component(cross_common)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/model/common.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/x86_64.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/mips.xm")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/model/common.xm"
     CROSS_COMMON_MODEL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/model/x86_64.xm"
     CROSS_X86_64_MODEL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/model/mips.xm"
     CROSS_MIPS_MODEL)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/shipped_models.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_models.cpp"
    @ONLY
)

add_library(cross_targets
    src/model/model.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_models.cpp"
    src/target/abi_lowering.cpp
    src/target/assembly_format.cpp
    src/target/registry.cpp
    src/target/subtarget.cpp
    src/target/mips/target.cpp
    src/target/x86_64/manual_endpoint.cpp
    src/target/x86_64/target.cpp
)
target_link_libraries(cross_targets PUBLIC cross_common)
cross_component(cross_targets)

add_library(cross_frontend
    src/frontend/ast.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/preprocessor.cpp
    src/frontend/procedural.cpp
    src/frontend/semantic.cpp
)
target_link_libraries(cross_frontend PUBLIC cross_common cross_targets)
cross_component(cross_frontend)

add_library(cross_middle
    src/middle/codegen_module.cpp
    src/middle/data_ir.cpp
    src/middle/hir.cpp
    src/middle/machine_ir.cpp
    src/middle/mir.cpp
    src/middle/mir_analysis.cpp
    src/middle/mir_pass.cpp
    src/middle/mir_transform.cpp
)
target_link_libraries(cross_middle PUBLIC cross_frontend cross_targets)
cross_component(cross_middle)

add_library(cross_raw_backend
    src/target/x86_64/raw_backend.cpp
)
target_link_libraries(cross_raw_backend PUBLIC cross_middle cross_targets)
cross_component(cross_raw_backend)

add_library(cross_native_backend
    src/backend/native/assembler.cpp
    src/backend/native/data_emitter.cpp
    src/backend/native/machine_pass.cpp
    src/backend/native/machine_pipeline.cpp
    src/backend/native/machine_transform.cpp
    src/backend/native/standalone_audit.cpp
    src/target/backend.cpp
    src/target/mips/backend.cpp
    src/target/mips/machine_description.cpp
    src/target/mips/native_backend.cpp
    src/target/x86_64/backend.cpp
    src/target/x86_64/dynamic_abi_plan.cpp
    src/target/x86_64/machine_description.cpp
    src/target/x86_64/manual_abi_plan.cpp
    src/target/x86_64/native_backend.cpp
)
target_link_libraries(cross_native_backend PUBLIC cross_middle cross_targets
                                                cross_raw_backend)
cross_component(cross_native_backend)

if(CROSS_ENABLE_LLVM_TEXT)
    add_library(cross_llvm_debug
        src/backend/llvm/llvm_ir.cpp
        src/backend/llvm/mir_ir.cpp
    )
    target_link_libraries(cross_llvm_debug PUBLIC cross_frontend cross_targets
                                                  cross_middle cross_raw_backend)
    cross_component(cross_llvm_debug)
endif()

if(CROSS_ENABLE_GCC_GIMPLE_TEXT)
    add_library(cross_gcc_gimple_debug
        src/backend/gcc/gimple_ir.cpp
    )
    target_link_libraries(cross_gcc_gimple_debug PUBLIC cross_frontend
        cross_targets cross_middle cross_raw_backend)
    cross_component(cross_gcc_gimple_debug)
endif()

add_library(cross_driver
    src/driver/driver.cpp
)
target_link_libraries(cross_driver PUBLIC cross_native_backend)
if(CROSS_ENABLE_LLVM_TEXT)
    target_link_libraries(cross_driver PRIVATE cross_llvm_debug)
    target_compile_definitions(cross_driver PRIVATE CROSS_ENABLE_LLVM_TEXT=1)
else()
    target_compile_definitions(cross_driver PRIVATE CROSS_ENABLE_LLVM_TEXT=0)
endif()
if(CROSS_ENABLE_GCC_GIMPLE_TEXT)
    target_link_libraries(cross_driver PRIVATE cross_gcc_gimple_debug)
    target_compile_definitions(cross_driver PRIVATE
        CROSS_ENABLE_GCC_GIMPLE_TEXT=1)
else()
    target_compile_definitions(cross_driver PRIVATE
        CROSS_ENABLE_GCC_GIMPLE_TEXT=0)
endif()
cross_component(cross_driver)

add_executable(cross_cc src/driver/cc_main.cpp)
target_link_libraries(cross_cc PRIVATE cross_driver)
set_target_properties(cross_cc PROPERTIES OUTPUT_NAME cc)
cross_tool(cross_cc)

add_executable(cross_cpp src/driver/cpp_main.cpp)
target_link_libraries(cross_cpp PRIVATE cross_driver)
set_target_properties(cross_cpp PROPERTIES OUTPUT_NAME cpp)
cross_tool(cross_cpp)

include(CTest)
if(BUILD_TESTING)
    add_executable(cross_options_test test/common/options_test.cpp)
    target_link_libraries(cross_options_test PRIVATE cross_targets)
    cross_component(cross_options_test)

    add_executable(cross_mir_verifier_test test/middle/mir_verifier_test.cpp)
    target_link_libraries(cross_mir_verifier_test PRIVATE cross_middle)
    cross_component(cross_mir_verifier_test)

    add_executable(cross_mir_analysis_test test/middle/mir_analysis_test.cpp)
    target_link_libraries(cross_mir_analysis_test PRIVATE cross_middle)
    cross_component(cross_mir_analysis_test)

    add_executable(cross_machine_ir_test test/middle/machine_ir_test.cpp)
    target_link_libraries(cross_machine_ir_test PRIVATE cross_middle)
    cross_component(cross_machine_ir_test)

    add_executable(cross_machine_transform_test
        test/backend/native/machine_transform_test.cpp)
    target_link_libraries(cross_machine_transform_test
        PRIVATE cross_native_backend)
    cross_component(cross_machine_transform_test)

    add_executable(cross_abi_rules_test test/model/abi_rules_test.cpp)
    target_link_libraries(cross_abi_rules_test PRIVATE cross_native_backend)
    cross_component(cross_abi_rules_test)

    add_executable(cross_manual_abi_plan_test test/backend/abi/manual_abi_plan_test.cpp)
    target_link_libraries(cross_manual_abi_plan_test PRIVATE cross_native_backend)
    cross_component(cross_manual_abi_plan_test)

    add_test(NAME cross_help COMMAND $<TARGET_FILE:cross_cc> --help)
    add_test(NAME cross_options COMMAND $<TARGET_FILE:cross_options_test>)
    add_test(NAME cross_mir_analysis
             COMMAND $<TARGET_FILE:cross_mir_analysis_test>)
    add_test(NAME cross_machine_ir
             COMMAND $<TARGET_FILE:cross_machine_ir_test>)
    add_test(NAME cross_machine_transform
             COMMAND $<TARGET_FILE:cross_machine_transform_test>)
    add_test(NAME cross_version COMMAND $<TARGET_FILE:cross_cc> --version)
    add_test(NAME cross_targets COMMAND $<TARGET_FILE:cross_cc> --print-targets)
    add_test(NAME cross_abis COMMAND $<TARGET_FILE:cross_cc> --print-abis)
    add_test(NAME cross_mips_vr4300_o32
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-o32
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.cmake")
    add_test(NAME cross_mips_optimization_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/optimization/codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-optimization-codegen
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/optimization/codegen.cmake")
    add_test(NAME cross_mips_allegrex
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DMIPS1_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/mips1_load.x
                -DF64_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/single_float_error.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-allegrex
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.cmake")
    add_test(NAME cross_mips_allegrex_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DRUNTIME=${CMAKE_SOURCE_DIR}/test/target/mips/psp/runtime.x
                -DMODULE_INFO=${CMAKE_SOURCE_DIR}/test/target/mips/psp/module_info.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/psp/link.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-allegrex-runtime
                -DPPSSPP=${CROSS_PPSSPP_HEADLESS}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/psp/runtime.cmake")
    add_test(NAME cross_mips_vr4300_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-runtime
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/runtime.cmake")
    add_test(NAME cross_model_system
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DMODEL_PATH=${CMAKE_SOURCE_DIR}/test/model/model_path
                -DABI_DSL=${CMAKE_SOURCE_DIR}/test/model/abi_dsl.y
                -DINVALID_ABI=${CMAKE_SOURCE_DIR}/test/model/invalid_abi
                -DINVALID_MANGLER=${CMAKE_SOURCE_DIR}/test/model/invalid_mangler.y
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/model/model_profile.x
                -DCOMPOSITION_SOURCE=${CMAKE_SOURCE_DIR}/test/model/abi_mangling_composition.x
                -DATTRIBUTE_ERROR=${CMAKE_SOURCE_DIR}/test/model/attribute_context_errors.x
                -DUNKNOWN_ATTRIBUTE_ERROR=${CMAKE_SOURCE_DIR}/test/model/unknown_attribute_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-profile.s
                -P "${CMAKE_SOURCE_DIR}/test/model/model_system.cmake")
    add_test(NAME cross_mangling_dsl
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/model/mangling_symbols.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mangling-dsl
                -P "${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.cmake")
    add_test(NAME cross_operator_binding
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/operator_binding.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/language/operator_binding_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/operator-binding.ll
                -P "${CMAKE_SOURCE_DIR}/test/language/operator_binding.cmake")
    add_test(NAME cross_language_surface
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/language/language_surface_errors.x
                -P "${CMAKE_SOURCE_DIR}/test/language/language_surface.cmake")
    add_test(NAME cross_basic_ir
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm
                "${CMAKE_SOURCE_DIR}/test/language/basic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/basic.ll")
    add_test(NAME cross_basic_object
        COMMAND $<TARGET_FILE:cross_cc> -c -O2 -ffunction-sections
                "${CMAKE_SOURCE_DIR}/test/language/basic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/basic.o")
    if(CROSS_ENABLE_GCC_GIMPLE_TEXT)
        find_program(CROSS_TEST_GCC NAMES gcc)
        if(CROSS_TEST_GCC)
            add_test(NAME cross_gcc_gimple_bridge
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_bridge.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_bridge_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-bridge
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_bridge.cmake")
        endif()
    endif()
    add_test(NAME cross_namespace
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/namespace.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/namespace.s")
    add_test(NAME cross_readable_symbols
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/readable-symbols
                -P "${CMAKE_SOURCE_DIR}/test/model/readable_symbols.cmake")
    add_test(NAME cross_instructions
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/machine/instructions.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/instructions.s")
    add_test(NAME cross_machine_builtin_semantics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/instructions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/instructions.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/machine_builtins.cmake")
    add_test(NAME cross_raw_naked
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-naked
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.cmake")
    add_test(NAME cross_raw_instruction_features
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/instruction_features.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-instruction-features
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/instruction_features.cmake")
    add_test(NAME cross_raw_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/isa_families.cmake")
    add_test(NAME cross_raw_base_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_families.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-base-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_families.cmake")
    add_test(NAME cross_raw_simd_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/simd_isa_families.x
                -DQUERY_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/simd_instruction_queries.x
                -DCLASS_ERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/opmask_class_error.x
                -DFEATURE_ERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/opmask_feature_error.x
                -DK0_ERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/opmask_k0_error.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-simd-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/simd_isa_families.cmake")
    add_test(NAME cross_raw_packed_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/packed_isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-packed-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/packed_isa_families.cmake")
    add_test(NAME cross_raw_x87_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_families.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-x87-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_families.cmake")
    add_test(NAME cross_raw_modern_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/modern_isa_families.x
                -DQUERY_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/modern_instruction_queries.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-modern-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/modern_isa_families.cmake")
    add_test(NAME cross_raw_system_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/system_isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-system-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/system_isa_families.cmake")
    add_test(NAME cross_raw_naked_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_errors.x
                -DINTERFACE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_interface_errors.x
                -DCONTEXT_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_instruction_context_errors.x
                -DRECOVERY_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_parser_recovery_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-naked-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_errors.cmake")
    set_tests_properties(cross_raw_naked_diagnostics PROPERTIES TIMEOUT 10)
    add_test(NAME cross_raw_cfg
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-cfg
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg.cmake")
    add_test(NAME cross_raw_cfg_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_errors.x
                -DHIR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_hir_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-cfg-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_errors.cmake")
    add_test(NAME cross_x86_64_relocation_models
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/relocation_models.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/relocation-models
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/relocation_models.cmake")
    add_test(NAME cross_x86_64_tls_models
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_models.x
                -DLARGE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_large_local.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_model_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tls-models
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/tls_models.cmake")
    add_test(NAME cross_x86_64_coff_unwind
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DALIGNED_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.x
                -DVLA_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                -DPROBE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/coff-unwind
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/coff_unwind.cmake")
    add_test(NAME cross_managed_mir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/managed-mir
                -P "${CMAKE_SOURCE_DIR}/test/middle/managed_mir.cmake")
    add_test(NAME cross_control_intrinsics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/control-intrinsics
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics.cmake")
    add_test(NAME cross_data_ir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_object_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-objects
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.cmake")
    add_test(NAME cross_managed_inlining
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/inlining.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/managed-inlining
                -P "${CMAKE_SOURCE_DIR}/test/middle/inlining.cmake")
    add_test(NAME cross_call_cloning
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/call_cloning.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/call-cloning
                -P "${CMAKE_SOURCE_DIR}/test/middle/call_cloning.cmake")
    add_test(NAME cross_optimization_flags
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/optimization_flags.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/optimization-flags
                -P "${CMAKE_SOURCE_DIR}/test/middle/optimization_flags.cmake")
    add_test(NAME cross_x86_64_feature_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_feature_codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-feature-codegen
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_feature_codegen.cmake")
    add_test(NAME cross_x86_64_floating_reinterpret_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/floating_reinterpret_allocation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-reinterpret-allocation
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/floating_reinterpret_allocation.cmake")
    find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cross_x86_64_benchmark_driver
            COMMAND "${Python3_EXECUTABLE}" -m pytest -q -p no:cacheprovider
                    "${CMAKE_SOURCE_DIR}/benchmark/x86_64/test_run.py")
        add_test(NAME cross_mips32_benchmark_driver
            COMMAND "${Python3_EXECUTABLE}" -m pytest -q -p no:cacheprovider
                    "${CMAKE_SOURCE_DIR}/benchmark/mips32/test_run.py")
    endif()
    add_test(NAME cross_x86_64_isa_selection
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_isa_selection.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-isa-selection
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_isa_selection.cmake")
    add_test(NAME cross_register_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/register-allocation
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.cmake")
    add_test(NAME cross_x86_64_block_layout
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/block_layout.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/block-layout
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/block_layout.cmake")
    add_test(NAME cross_x86_64_if_conversion
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/if_conversion.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/if-conversion
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/if_conversion.cmake")
    add_test(NAME cross_x86_64_loop_unrolling
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_unrolling.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/loop-unrolling
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/loop_unrolling.cmake")
    add_test(NAME cross_x86_64_slp_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/slp_vectorization.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/slp-vectorization
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/slp_vectorization.cmake")
    add_test(NAME cross_x86_64_red_zone
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/red_zone.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/red-zone
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/red_zone.cmake")
    add_test(NAME cross_x86_64_stack_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stack-alignment
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.cmake")
    add_test(NAME cross_x86_64_dynamic_call_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_call_alignment.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-call-alignment
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_call_alignment.cmake")
    add_test(NAME cross_x86_64_static_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-stack-probe
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.cmake")
    add_test(NAME cross_x86_64_dynamic_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-stack-probe
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_stack_probe.cmake")
    add_test(NAME cross_x86_64_overaligned_vla
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/overaligned-vla
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.cmake")
    add_test(NAME cross_f128_native
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/f128.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/f128-native
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/f128.cmake")
    add_test(NAME cross_runtime_managed_mir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-managed-mir.ll
                -DENTRY=managed_mir_entry
                -DEXPECTED=72
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_managed_mir_o2
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCC_FLAGS=-O2
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-managed-mir-o2.ll
                -DENTRY=managed_mir_entry
                -DEXPECTED=72
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_control_intrinsics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-control-intrinsics
                -DENTRY=control_intrinsics_entry
                -DEXPECTED=7
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_raw_structured_cfg
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-raw-structured-cfg
                -DENTRY=raw_structured
                -DEXPECTED=42
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_register_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-register-allocation
                -DENTRY=register_allocation_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_loop_rotation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_rotation.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-loop-rotation
                -DENTRY=loop_rotation_entry
                -DEXPECTED=0
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_parallel_copy
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/parallel_copy_runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-parallel-copy
                -DENTRY=parallel_copy_entry
                -DEXPECTED=1
                -DCC_FLAGS=-Oz
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_if_conversion
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/if_conversion.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-if-conversion
                -DENTRY=if_conversion_entry
                -DEXPECTED=14
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_loop_unrolling
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_unrolling.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-loop-unrolling
                -DENTRY=loop_unrolling_entry
                -DEXPECTED=35
                -DCC_FLAGS=-O3
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_slp_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/slp_vectorization.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-slp-vectorization
                -DENTRY=slp_vectorization_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_stack_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-stack-alignment
                -DENTRY=stack_alignment_entry
                -DEXPECTED=2
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_dynamic_call_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_call_alignment.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-dynamic-call-alignment
                -DENTRY=dynamic_call_alignment_entry
                -DEXPECTED=13
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_static_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-static-stack-probe
                -DENTRY=static_stack_probe_entry
                -DEXPECTED=58
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_dynamic_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_stack_probe.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-dynamic-stack-probe
                -DENTRY=dynamic_stack_probe_entry
                -DEXPECTED=42
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_overaligned_vla
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-overaligned-vla
                -DENTRY=overaligned_vla_entry
                -DEXPECTED=46
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_f128
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/f128.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-f128
                -DENTRY=f128_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_f128_differential
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/f128_random.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-f128-differential
                -DENTRY=f128_random_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_mir_verifier COMMAND $<TARGET_FILE:cross_mir_verifier_test>)
    add_test(NAME cross_abi_rules
        COMMAND $<TARGET_FILE:cross_abi_rules_test>
                "${CMAKE_SOURCE_DIR}/test/model/abi_dsl.y")
    add_test(NAME cross_manual_abi_plan
        COMMAND $<TARGET_FILE:cross_manual_abi_plan_test>)
    add_test(NAME cross_x86_64_aggregate_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport_runner.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-transport
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport.cmake")
    add_test(NAME cross_dynamic_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/dynamic_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-abi
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/dynamic_abi.cmake")
    add_test(NAME cross_runtime_dynamic_abi_clobbers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/dynamic_abi.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-dynamic-abi-clobbers
                -DENTRY=dynamic_clobber_runtime_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O3
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_native_backend
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend_runtime.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-backend
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.cmake")
    add_test(NAME cross_macho_object
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DNATIVE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.x
                -DRAW_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.x
                -DDATA_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.x
                -DNAMESPACE_SOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/macho-object
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/macho_object.cmake")
    add_test(NAME cross_vectors
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/vector/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/vectors.s")
    add_test(NAME cross_runtime_vectors_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vectors.ll
                -DENTRY=vector_entry
                -DEXPECTED=47
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_vectors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vectors
                -DENTRY=vector_entry
                -DEXPECTED=47
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_vector_loop_optimization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/loop_optimization.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-loop-optimization.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/loop_optimization.cmake")
    add_test(NAME cross_runtime_native_vector_loop_optimization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/loop_optimization.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vector-loop-optimization
                -DENTRY=vector_loop_entry
                -DEXPECTED=16
                "-DCC_FLAGS=-O2;-march=x86-64-v3"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_x86_64_early_exit_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/early_exit.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/early-exit-vectorization.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/early_exit.cmake")
    add_test(NAME cross_runtime_x86_64_early_exit_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/early_exit.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-early-exit-vectorization
                -DENTRY=early_exit_entry
                -DEXPECTED=16
                "-DCC_FLAGS=-O3;-march=x86-64-v3"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_wide_vectors
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/wide-vectors.s")
    add_test(NAME cross_runtime_wide_vectors_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wide-vectors.ll
                -DENTRY=wide_vector_entry
                -DEXPECTED=76
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_wide_vectors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-wide-vectors
                -DENTRY=wide_vector_entry
                -DEXPECTED=76
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_registered_wide_vector_features
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/registered-wide-vector
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/automatic_wide_vector_features.cmake")
    add_test(NAME cross_x86_64_vector_extensions
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/extensions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-vector-extensions
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/extensions.cmake")
    add_test(NAME cross_vector_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/vector
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/diagnostics.cmake")
    add_test(NAME cross_numeric_conversions
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/conversion/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/numeric-conversions.s")
    add_test(NAME cross_runtime_numeric_conversions_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/conversion/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/numeric-conversions.ll
                -DENTRY=conversion_entry
                -DEXPECTED=41
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_numeric_conversions
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/conversion/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-numeric-conversions
                -DENTRY=conversion_entry
                -DEXPECTED=41
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_arrays
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/array/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/arrays.s")
    add_test(NAME cross_runtime_arrays_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/arrays.ll
                -DENTRY=array_entry
                -DEXPECTED=44
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_arrays
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-arrays
                -DENTRY=array_entry
                -DEXPECTED=44
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_vla
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/vla.s")
    add_test(NAME cross_runtime_vla_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vla.ll
                -DENTRY=vla_entry
                -DEXPECTED=80
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_vla
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vla
                -DENTRY=vla_entry
                -DEXPECTED=80
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_array_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/array
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/array-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/array/diagnostics.cmake")
    add_test(NAME cross_aggregates
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/aggregates.s")
    add_test(NAME cross_runtime_aggregates_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregates.ll
                -DENTRY=aggregate_entry
                -DEXPECTED=77
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_aggregates
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-aggregates
                -DENTRY=aggregate_entry
                -DEXPECTED=77
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_aggregate_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/diagnostics.cmake")
    add_test(NAME cross_atomics
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/atomic/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/atomics.s")
    add_test(NAME cross_runtime_atomics_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/atomic/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomics.ll
                -DENTRY=atomic_entry
                -DEXPECTED=35
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_atomics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/atomic/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-atomics
                -DENTRY=atomic_entry
                -DEXPECTED=35
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_atomic_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/atomic
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomic-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/atomic/diagnostics.cmake")
    add_test(NAME cross_variadics
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/variadics.s")
    add_test(NAME cross_runtime_native_variadics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-variadics
                -DENTRY=variadic_entry
                -DEXPECTED=9
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_variadic_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/variadic
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadic-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/diagnostics.cmake")
    if(CROSS_ENABLE_LLVM_TEXT)
        add_test(NAME cross_variadics_llvm_debug
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DLLVM_AS=llvm-as
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadics-debug
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/llvm.cmake")
    endif()
    add_test(NAME cross_runtime_native_automatic_call_boundary
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/automatic_call_boundary.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-automatic-call-boundary
                -DENTRY=automatic_stack_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_loops
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/loops.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/loops.s")
    add_test(NAME cross_compile_time
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/compile_time.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/compile_time.s")
    add_test(NAME cross_pointers
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/pointers.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/pointers.s")
    add_test(NAME cross_runtime_native_addresses
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/pointers.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-addresses
                -DENTRY=address_entry
                -DEXPECTED=107
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_fast_math
        COMMAND $<TARGET_FILE:cross_cc> -S -O3 -ffast-math
                "${CMAKE_SOURCE_DIR}/test/language/floating.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/floating.s")
    add_test(NAME cross_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/abi.s")
    add_test(NAME cross_default_cross_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross_default.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-default-abi
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross_default.cmake")
    add_test(NAME cross_cross_abi_separate_compilation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross/callee.x
                -DCALLER_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross/caller.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-abi-separate
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross/separate_compilation.cmake")
    add_test(NAME cross_manual_register_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-abi.s")
    add_test(NAME cross_runtime_manual_register_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-abi.ll
                -DENTRY=manual_abi_entry
                -DEXPECTED=62
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_manual_simd_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-simd-abi.ll
                -DENTRY=manual_simd_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_manual_simd_bit_transport
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_simd_transport.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-simd-transport.ll")
    add_test(NAME cross_runtime_native_manual_simd_bit_transport
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_simd_transport.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-simd-transport
                -DENTRY=simd_transport_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_manual_simd_pointer_transport
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_simd_pointer_transport.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-simd-pointer-transport.ll
                -DENTRY=pointer_simd_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_manual_register_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_errors.cmake")
    add_test(NAME cross_manual_register_sysv_optimizer
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_sysv_optimizer.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-sysv
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_sysv_optimizer.cmake")
    add_test(NAME cross_manual_register_pairs
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-pairs.s")
    add_test(NAME cross_runtime_manual_register_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-pairs.ll
                -DENTRY=paired_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_register_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-register-pairs
                -DENTRY=paired_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_register_pair_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-pair-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair_errors.cmake")
    add_test(NAME cross_manual_register_pairs_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-pairs-sysv.s")
    add_test(NAME cross_manual_register_indirect_pairs
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pairs.s")
    add_test(NAME cross_runtime_manual_register_indirect_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-indirect-pairs.ll
                -DENTRY=indirect_pair_entry
                -DEXPECTED=116
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_register_indirect_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-register-indirect-pairs
                -DENTRY=indirect_pair_entry
                -DEXPECTED=116
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_register_indirect_pair_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pair-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair_errors.cmake")
    add_test(NAME cross_manual_register_indirect_pairs_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pairs-sysv.s")
    add_test(NAME cross_manual_register_mixed
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed.s")
    add_test(NAME cross_runtime_manual_register_mixed
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-mixed.ll
                -DENTRY=mixed_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_register_mixed
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-register-mixed
                -DENTRY=mixed_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_register_mixed_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed_errors.cmake")
    add_test(NAME cross_manual_register_mixed_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed-sysv.s")
    add_test(NAME cross_manual_stack_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-stack-abi.s")
    add_test(NAME cross_runtime_manual_stack_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-stack-abi.ll
                -DENTRY=manual_stack_entry
                -DEXPECTED=74
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_stack_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-stack-abi
                -DENTRY=manual_stack_entry
                -DEXPECTED=74
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_stack_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-stack-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_errors.cmake")
    add_test(NAME cross_manual_stack_abi_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-stack-abi-sysv.s")
    add_test(NAME cross_manual_memory_wide
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-memory-wide.s")
    add_test(NAME cross_runtime_manual_memory_wide
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-memory-wide.ll
                -DENTRY=manual_memory_wide_entry
                -DEXPECTED=9
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_memory_wide
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-memory-wide
                -DENTRY=manual_memory_wide_entry
                -DEXPECTED=9
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_memory_wide_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-memory-wide-sysv.s")
    add_test(NAME cross_manual_aggregate
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_aggregate.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-aggregate.s")
    add_test(NAME cross_runtime_native_manual_aggregate
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_aggregate.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-aggregate
                -DENTRY=manual_aggregate_entry
                -DEXPECTED=8
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_runtime_native_manual_aggregate_o2
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCC_FLAGS=-O2
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_aggregate.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-aggregate-o2
                -DENTRY=manual_aggregate_entry
                -DEXPECTED=8
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_hard_register
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/hard-register.s")
    add_test(NAME cross_runtime_hard_register
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-hard-register.ll
                -DENTRY=hard_register_entry
                -DEXPECTED=11
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_hard_register
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-hard-register
                -DENTRY=hard_register_entry
                -DEXPECTED=11
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_hard_register_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hard-register-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register_errors.cmake")
    add_test(NAME cross_hard_register_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/hard-register-sysv.s")
    add_test(NAME cross_manual_wide_simd
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mavx512f
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd.s")
    add_test(NAME cross_manual_wide_simd_object
        COMMAND $<TARGET_FILE:cross_cc> -c -O2 -mavx512f
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd.o")
    add_test(NAME cross_runtime_manual_wide_simd_vectors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_vector_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-wide-simd.ll
                -DENTRY=wide_simd_vector_entry
                -DEXPECTED=2
                -DCC_FLAGS=-mavx
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_manual_ymm_scalar
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_ymm_scalar.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-ymm-scalar.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_ymm_runtime.cmake")
    add_test(NAME cross_manual_wide_simd_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd_errors.cmake")
    add_test(NAME cross_manual_wide_simd_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mavx512f
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd-sysv.s")
    add_test(NAME cross_manual_x87
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-x87.s")
    add_test(NAME cross_runtime_manual_x87
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-x87.ll
                -DENTRY=x87_entry
                -DEXPECTED=23
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_native_manual_x87
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-manual-x87
                -DENTRY=x87_entry
                -DEXPECTED=23
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_manual_x87_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-x87-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_errors.cmake")
    add_test(NAME cross_manual_x87_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-x87-sysv.s")
    add_test(NAME cross_manual_x87_standalone
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-x87-standalone
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_standalone.cmake")
    add_test(NAME cross_registers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/register_registry.cmake")
    add_test(NAME cross_compilation_group
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/group_decl.x"
                "${CMAKE_SOURCE_DIR}/test/language/group_def.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/group.s")
    add_test(NAME cross_source_unit_static
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/static_one.x"
                "${CMAKE_SOURCE_DIR}/test/language/static_two.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/static.s")
    add_test(NAME cross_labels
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/labels.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/labels.s")
    add_test(NAME cross_preprocessor
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/preprocessor.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/preprocessor.i
                -P "${CMAKE_SOURCE_DIR}/test/frontend/preprocessor.cmake")
    add_test(NAME cross_diagnostic
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm
                "${CMAKE_SOURCE_DIR}/test/frontend/diagnostic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/diagnostic.ll")
    set_tests_properties(cross_diagnostic PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cross_standalone_audit
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/basic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/standalone.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/standalone.cmake")
    add_test(NAME cross_runtime_parameter_copyout
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/basic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-basic.ll
                -DENTRY=entry
                -DEXPECTED=6
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_namespace
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-namespace.ll
                -DENTRY=app::entry
                -DEXPECTED=41
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_runtime_computed_goto
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/labels.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-labels.ll
                -DENTRY=dispatch_right
                -DEXPECTED=22
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    add_test(NAME cross_volatile_contract
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/volatile.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/volatile.ll
                -P "${CMAKE_SOURCE_DIR}/test/language/volatile.cmake")
    add_test(NAME cross_semantic_expansion
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/semantic_expansion.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/semantic-expansion
                -P "${CMAKE_SOURCE_DIR}/test/middle/semantic_expansion.cmake")
    set_tests_properties(cross_semantic_expansion PROPERTIES TIMEOUT 15)
    add_test(NAME cross_default_evaluation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/default_evaluation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/default-evaluation
                -P "${CMAKE_SOURCE_DIR}/test/middle/default_evaluation.cmake")
    add_test(NAME cross_runtime_native_default_evaluation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/default_evaluation.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-default-evaluation
                -DENTRY=evaluation_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    add_test(NAME cross_evaluation_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/evaluation_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evaluation-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/middle/evaluation_errors.cmake")
    add_test(NAME cross_procedural_origin_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_origin_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-origin-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_origin_errors.cmake")
    add_test(NAME cross_procedural_namespace
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-namespace.ll
                -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_namespace.cmake")
    add_test(NAME cross_patch_value_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_value_errors.x
                -DCONFLICT_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_sink_conflict.x
                -DGENERIC_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_generic_conflict.x
                -DINLINE_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_inline_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-value-errors
                -P "${CMAKE_SOURCE_DIR}/test/middle/patch_value_errors.cmake")
    set_tests_properties(cross_patch_value_diagnostics PROPERTIES TIMEOUT 10)
endif()
