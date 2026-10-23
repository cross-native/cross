cmake_minimum_required(VERSION 3.20)
project(cross VERSION 0.1.0 LANGUAGES CXX)

option(CROSS_ENABLE_LLVM_TEXT
       "Build the optional LLVM text debug/compatibility serializer" ON)
option(CROSS_ENABLE_GCC_GIMPLE_TEXT
       "Build the optional GCC GIMPLE debug/backend-validation serializer" ON)
option(CROSS_STATIC_TOOLS
       "Link the cc and cpp tools without shared compiler runtimes" ON)
set(CROSS_DEFAULT_TARGET "" CACHE STRING
    "Target triple used without -target or -mprofile; empty selects the host")
set(CROSS_PPSSPP_HEADLESS "" CACHE FILEPATH
    "Optional PPSSPPHeadless executable for Allegrex runtime tests")
option(CROSS_TEST_CROSS_LD
       "Link the MIPS test images with the Cross ld instead of ld.lld" OFF)

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
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(CROSS_DEFAULT_TARGET)
    set(cross_default_target "${CROSS_DEFAULT_TARGET}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cross_host_arch)
    if(cross_host_arch MATCHES "^(amd64|x64)$")
        set(cross_host_arch x86_64)
    elseif(cross_host_arch STREQUAL "arm64")
        set(cross_host_arch aarch64)
    endif()
    if(CMAKE_SYSTEM_NAME MATCHES "^(Windows|MSYS|CYGWIN)$")
        set(cross_default_target "${cross_host_arch}-w64-windows-gnu")
    elseif(APPLE)
        set(cross_default_target "${cross_host_arch}-apple-darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(cross_default_target "${cross_host_arch}-unknown-linux-gnu")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" cross_host_system)
        set(cross_default_target "${cross_host_arch}-unknown-${cross_host_system}")
    endif()
endif()
message(STATUS "Cross default target: ${cross_default_target}")

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
    src/common/builtin_type.cpp
    src/common/diagnostic.cpp
    src/common/floating_bits.cpp
    src/common/floating_semantics.cpp
    src/common/options.cpp
    src/common/source.cpp
)
cross_component(cross_common)
target_compile_definitions(cross_common PRIVATE
    CROSS_DEFAULT_TARGET="${cross_default_target}")

add_library(cross_assembler
    src/assembler/assembler.cpp
    src/assembler/coff_writer.cpp
    src/assembler/elf_writer.cpp
    src/assembler/macho_writer.cpp
)
target_link_libraries(cross_assembler PUBLIC cross_common)
cross_component(cross_assembler)

add_library(cross_linker
    src/linker/eh_frame.cpp
    src/linker/input.cpp
    src/linker/layout.cpp
    src/linker/link.cpp
    src/linker/merge.cpp
    src/linker/output.cpp
    src/linker/relocatable.cpp
    src/linker/script.cpp
)
target_link_libraries(cross_linker PUBLIC cross_assembler)
cross_component(cross_linker)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/model/common.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/aarch64.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/aarch64_instructions.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/x86_64.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/mips.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/model/mips_instructions.xm"
             "${CMAKE_CURRENT_SOURCE_DIR}/lib/format.x")
# A shipped model's text as raw string literals of at most 16000 bytes, the
# length every C++ compiler accepts.
function(cross_model_text variable path)
    file(READ "${path}" text)
    string(LENGTH "${text}" length)
    set(pieces "")
    set(offset 0)
    while(offset LESS length)
        string(SUBSTRING "${text}" ${offset} 16000 piece)
        string(APPEND pieces "R\"cross_model(${piece})cross_model\",\n")
        math(EXPR offset "${offset} + 16000")
    endwhile()
    set(${variable} "${pieces}" PARENT_SCOPE)
endfunction()
cross_model_text(CROSS_COMMON_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/model/common.xm")
cross_model_text(CROSS_AARCH64_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/model/aarch64.xm")
cross_model_text(CROSS_AARCH64_INSTRUCTIONS_MODEL
                 "${CMAKE_CURRENT_SOURCE_DIR}/model/aarch64_instructions.xm")
cross_model_text(CROSS_X86_64_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/model/x86_64.xm")
cross_model_text(CROSS_MIPS_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/model/mips.xm")
cross_model_text(CROSS_MIPS_INSTRUCTIONS_MODEL
                 "${CMAKE_CURRENT_SOURCE_DIR}/model/mips_instructions.xm")
cross_model_text(CROSS_FORMAT_CHECKERS "${CMAKE_CURRENT_SOURCE_DIR}/lib/format.x")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/shipped_models.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_models.cpp"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/driver/shipped_checkers.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_checkers.cpp"
    @ONLY
)

add_library(cross_targets
    src/model/instructions.cpp
    src/model/model.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_models.cpp"
    src/target/abi_lowering.cpp
    src/target/assembly_format.cpp
    src/target/registry.cpp
    src/target/subtarget.cpp
    src/target/aarch64/assembler.cpp
    src/target/aarch64/instructions.cpp
    src/target/aarch64/target.cpp
    src/target/mips/assembler.cpp
    src/target/mips/linker.cpp
    src/target/mips/target.cpp
    src/target/operand_encoders.cpp
    src/target/x86_64/assembler.cpp
    src/target/x86_64/encodings.cpp
    src/target/x86_64/manual_endpoint.cpp
    src/target/x86_64/target.cpp
    src/target/x86_64/vector_encodings.cpp
)
target_link_libraries(cross_targets PUBLIC cross_common cross_assembler cross_linker)
cross_component(cross_targets)

add_library(cross_frontend
    src/frontend/ast.cpp
    src/frontend/embed.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/parser_context.cpp
    src/frontend/preprocessor.cpp
    src/frontend/preprocessor_expression.cpp
    src/frontend/procedural.cpp
    src/frontend/semantic.cpp
    src/frontend/syntax.cpp
    src/frontend/syntax_tree.cpp
    src/frontend/token_tree.cpp
)
target_link_libraries(cross_frontend PUBLIC cross_common cross_targets)
cross_component(cross_frontend)

add_library(cross_middle
    src/middle/codegen_module.cpp
    src/middle/data_ir.cpp
    src/middle/hir.cpp
    src/middle/initializer.cpp
    src/middle/initialization.cpp
    src/middle/patch_sink.cpp
    src/middle/machine_ir.cpp
    src/middle/frame_ir.cpp
    src/middle/mir.cpp
    src/middle/mir_analysis.cpp
    src/middle/mir_bits.cpp
    src/middle/mir_memory.cpp
    src/middle/mir_pass.cpp
    src/middle/mir_ranges.cpp
    src/middle/mir_transform.cpp
    src/middle/mir_wide.cpp
    src/middle/shrink_wrap.cpp
)
target_link_libraries(cross_middle PUBLIC cross_frontend cross_targets)
cross_component(cross_middle)

add_library(cross_debug_info
    src/backend/native/debug_info.cpp
    src/backend/native/stack_usage.cpp
)
target_link_libraries(cross_debug_info PUBLIC cross_middle cross_targets)
cross_component(cross_debug_info)

add_library(cross_raw_backend
    src/target/x86_64/raw_backend.cpp
)
target_link_libraries(cross_raw_backend PUBLIC cross_middle cross_targets
                                             cross_debug_info)
cross_component(cross_raw_backend)

add_library(cross_native_backend
    src/backend/native/data_emitter.cpp
    src/backend/native/frame_cfi.cpp
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
    src/driver/stack.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shipped_checkers.cpp"
)
target_link_libraries(cross_driver PUBLIC cross_native_backend)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(cross_driver PRIVATE Threads::Threads)
endif()
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

add_executable(cross_as src/driver/as_main.cpp)
target_link_libraries(cross_as PRIVATE cross_targets)
set_target_properties(cross_as PROPERTIES OUTPUT_NAME as)
cross_tool(cross_as)

add_executable(cross_ld src/driver/ld_main.cpp)
target_link_libraries(cross_ld PRIVATE cross_targets)
set_target_properties(cross_ld PROPERTIES OUTPUT_NAME ld)
cross_tool(cross_ld)

include(CTest)
if(BUILD_TESTING)
    function(cross_syntax_test_labels name arch profile)
        set_property(TEST ${name} APPEND PROPERTY LABELS
            frontend feature.syntax arch.${arch} profile.${profile} ${ARGN})
        if(name MATCHES "^cross_meta_continuations_")
            set_property(TEST ${name} APPEND PROPERTY LABELS feature.meta feature.evaluation)
        endif()
        if(name MATCHES "^cross_syntax_helper_constants_")
            set_property(TEST ${name} APPEND PROPERTY LABELS
                feature.evaluation feature.source_constraints feature.pointer feature.vector)
        endif()
        if(name MATCHES "^cross_syntax_enum_depth_")
            set_property(TEST ${name} APPEND PROPERTY LABELS
                feature.evaluation feature.resources feature.enum)
        endif()
        if(name MATCHES "^cross_syntax_generic_depth_")
            set_property(TEST ${name} APPEND PROPERTY LABELS
                feature.generic feature.evaluation feature.resources)
        endif()
        if(name MATCHES "^cross_syntax_recognition_depth_")
            set_property(TEST ${name} APPEND PROPERTY LABELS
                feature.matcher feature.resources feature.syntax_tree)
        endif()
        if(name MATCHES "^cross_syntax_pointer_depth_")
            set_property(TEST ${name} APPEND PROPERTY LABELS
                feature.pointer feature.evaluation feature.resources)
        endif()
    endfunction()
    add_test(NAME cross_doc_examples
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/doc-examples
                     -P "${CMAKE_SOURCE_DIR}/test/language/doc_examples.cmake")
    set_tests_properties(cross_doc_examples PROPERTIES
        LABELS "feature.documentation;arch.x86_64;check.compile")
    add_test(NAME cross_callable_contract_types
             COMMAND $<TARGET_FILE:cross_cc> -S -target x86_64-unknown-linux-gnu
                     "${CMAKE_SOURCE_DIR}/test/language/callable_contract_types.x"
                     -o "${CMAKE_CURRENT_BINARY_DIR}/callable-contract-types.s")
    set_tests_properties(cross_callable_contract_types PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.calls")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_callable_custom_result_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/callable_custom_result.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/callable-custom-result-${level}
                         -DENTRY=callable_custom_result_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=--model=${CMAKE_SOURCE_DIR}/test/model/custom.y;-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_callable_custom_result_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.calls;feature.abi;opt.${level};profile.custom")
    endforeach()
    add_test(NAME cross_deep_nesting
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/deep-nesting
                     -P "${CMAKE_SOURCE_DIR}/test/language/deep_nesting.cmake")
    set_tests_properties(cross_deep_nesting PROPERTIES
        LABELS "feature.resources;arch.x86_64;check.compile")
    add_test(NAME cross_musttail_evaluation
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/musttail-evaluation
                     -P "${CMAKE_SOURCE_DIR}/test/language/musttail_evaluation.cmake")
    set_tests_properties(cross_musttail_evaluation PROPERTIES
        LABELS "feature.musttail;feature.evaluation;arch.x86_64;check.compile")
    add_test(NAME cross_compile_scaling
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/compile-scaling
                     -P "${CMAKE_SOURCE_DIR}/test/language/compile_scaling.cmake")
    set_tests_properties(cross_compile_scaling PROPERTIES
        LABELS "feature.resources;arch.x86_64;check.compile;opt.O0;opt.O2")
    add_test(NAME cross_storage_constraints
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/storage-constraints
                     -P "${CMAKE_SOURCE_DIR}/test/language/storage_constraints.cmake")
    set_tests_properties(cross_storage_constraints PROPERTIES
        LABELS "feature.storage;arch.x86_64;check.compile;diagnostics")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_may_alias_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/may_alias.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/may-alias-${level}
                         -DENTRY=may_alias_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_may_alias_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.attributes;feature.evaluation;opt.${level}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_runtime_label_relocations_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/label_relocations.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/label-relocations-${level}
                         -DENTRY=label_relocation_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_label_relocations_${level} PROPERTIES
            LABELS "backend.native;arch.x86_64;check.runtime;feature.labels;feature.relocation;feature.initializer;opt.${level}")
        add_test(NAME cross_runtime_abi_alias_redeclaration_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/abi_alias_redeclaration.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/abi-alias-redeclaration-${level}
                         -DENTRY=abi_alias_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_abi_alias_redeclaration_${level} PROPERTIES
            LABELS "backend.native;arch.x86_64;check.runtime;feature.abi;feature.linkage;opt.${level}")
    endforeach()
    add_test(NAME cross_may_alias_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/may_alias.x
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/may-alias-checks
                     -P "${CMAKE_SOURCE_DIR}/test/language/may_alias_checks.cmake")
    set_tests_properties(cross_may_alias_checks PROPERTIES
        LABELS "frontend;arch.x86_64;arch.mips;endian.big;endian.little;check.compile;diagnostics;feature.attributes;feature.evaluation")
    foreach(arch x86_64 mips)
        add_test(NAME cross_aligned_typedef_${arch}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/aligned_typedef.x
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-${arch}
                         -DARCH=${arch}
                         -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                         -DGIMPLE_TEXT=${CROSS_ENABLE_GCC_GIMPLE_TEXT}
                         -P "${CMAKE_SOURCE_DIR}/test/language/aligned_typedef.cmake")
        set_tests_properties(cross_aligned_typedef_${arch} PROPERTIES
            LABELS "frontend;diagnostics;feature.layout;feature.attributes;feature.records;feature.array;feature.evaluation;arch.${arch};check.compile")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_runtime_vector_explicit_splat_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/explicit_splat.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-explicit-splat-${level}
                         -DENTRY=explicit_splat_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_vector_explicit_splat_${level} PROPERTIES
            LABELS "backend.native;arch.x86_64;check.runtime;feature.vector;opt.${level}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_runtime_int128_shifts_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/int128_shifts.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/int128-shifts-${level}
                         -DENTRY=int128_shifts_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_int128_shifts_${level} PROPERTIES
            LABELS "backend.native;arch.x86_64;check.runtime;feature.integer128;opt.${level}")
    endforeach()
    foreach(variant O0 O2 O3 Oz O2_noalloc)
        string(REPLACE "_noalloc" ";-fno-register-allocation" phi_frame_flags "-${variant}")
        add_test(NAME cross_runtime_phi_frame_copies_${variant}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/phi_frame_copies.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/phi-frame-copies-${variant}
                         -DENTRY=phi_frame_entry
                         -DEXPECTED=61
                         "-DCC_FLAGS=${phi_frame_flags}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        string(REPLACE "_noalloc" ";opt.noalloc" phi_frame_levels "opt.${variant}")
        set_tests_properties(cross_runtime_phi_frame_copies_${variant} PROPERTIES
            LABELS "backend.native;arch.x86_64;check.runtime;feature.register_allocation;${phi_frame_levels}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_runtime_quote_local_types_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/quote_local_types.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/quote-local-types-${level}
                         -DENTRY=quote_local_types_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_quote_local_types_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.meta;opt.${level}")
        add_test(NAME cross_runtime_quote_local_records_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/quote_local_records.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/quote-local-records-${level}
                         -DENTRY=quote_local_records_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_quote_local_records_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.meta;feature.nominal;opt.${level}")
        add_test(NAME cross_runtime_quote_local_generic_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/quote_local_generic.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/quote-local-generic-${level}
                         -DENTRY=quote_local_generic_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_quote_local_generic_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.meta;feature.generic;feature.nominal;opt.${level}")
        add_test(NAME cross_runtime_parse_local_types_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/parse_local_types.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parse-local-types-${level}
                         -DENTRY=parse_local_types_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_parse_local_types_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.meta;feature.generic;feature.nominal;opt.${level}")
        add_test(NAME cross_runtime_generic_header_redeclaration_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_header_redeclaration.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-header-redeclaration-${level}
                         -DENTRY=generic_header_redeclaration_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_header_redeclaration_${level} PROPERTIES
            LABELS "frontend;backend.native;arch.x86_64;check.runtime;feature.generic;feature.nominal;opt.${level}")
    endforeach()
    add_test(NAME cross_quote_local_records_errors
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/quote-local-records-errors
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/quote_local_records_errors.cmake")
    set_tests_properties(cross_quote_local_records_errors PROPERTIES
        LABELS "frontend;arch.x86_64;check.compile;diagnostics;feature.meta;feature.nominal")
    add_test(NAME cross_generic_header_redeclaration_errors
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-header-redeclaration-errors
                     -P "${CMAKE_SOURCE_DIR}/test/language/generic_header_redeclaration_errors.cmake")
    set_tests_properties(cross_generic_header_redeclaration_errors PROPERTIES
        LABELS "frontend;arch.x86_64;check.compile;diagnostics;feature.generic;feature.nominal")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_generic_angle_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_angle.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-angle-${level}
                         -DENTRY=generic_angle_entry
                         -DEXPECTED=1
                         -DCC_FLAGS=-${level}
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_angle_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic;opt.${level}")
        add_test(NAME cross_runtime_generic_angle_custom_abi_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_angle_custom_abi.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-angle-custom-abi-${level}
                         -DENTRY=generic_angle_custom_abi_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=--model=${CMAKE_SOURCE_DIR}/test/model/custom.y;-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_angle_custom_abi_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic;feature.abi;opt.${level};profile.custom")
        add_test(NAME cross_runtime_generic_angle_default_odd_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_angle_custom_abi.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-angle-default-odd-${level}
                         -DENTRY=generic_angle_custom_abi_entry
                         -DEXPECTED=1
                         "-DCC_FLAGS=--model=${CMAKE_SOURCE_DIR}/test/model/custom.y;-mabi=odd_abi;-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_angle_default_odd_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic;opt.${level};profile.custom")
    endforeach()
    add_test(NAME cross_generic_angle_errors
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language
                     -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generic-angle-errors
                     -P "${CMAKE_SOURCE_DIR}/test/language/generic_angle_errors.cmake")
    set_tests_properties(cross_generic_angle_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.generic")
    foreach(profile host odd)
        set(generic_noalloc_flags
            "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y"
            -O2 -fno-register-allocation -fno-eval-calls)
        if(profile STREQUAL odd)
            list(APPEND generic_noalloc_flags -mabi=odd_abi)
        endif()
        add_test(NAME cross_runtime_generic_noalloc_${profile}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_angle_custom_abi.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-noalloc-${profile}
                -DENTRY=generic_angle_custom_abi_entry -DEXPECTED=1
                "-DCC_FLAGS=${generic_noalloc_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_noalloc_${profile} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic")
    endforeach()
    add_test(NAME cross_generic_actual_floats
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_actual_floats.x
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-actual-floats
                     -P "${CMAKE_SOURCE_DIR}/test/language/generic_actual_floats.cmake")
    set_tests_properties(cross_generic_actual_floats PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;feature.generic;profile.custom")
    add_test(NAME cross_generic_assertions
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_assertions.x
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-assertions
                     -P "${CMAKE_SOURCE_DIR}/test/language/generic_assertions.cmake")
    set_tests_properties(cross_generic_assertions PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;feature.generic;feature.assertions;profile.custom")
    add_test(NAME cross_block_assertion_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/block-assertions
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/block_assertions.cmake")
    set_tests_properties(cross_block_assertion_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.assertions")
    foreach(mode native custom mips mipsel mips64 mips64el)
        add_test(NAME cross_meta_assertions_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-assertions-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_assertions.cmake")
        if(mode MATCHES "^mips")
            set(assertion_arch mips)
        else()
            set(assertion_arch x86_64)
        endif()
        set_tests_properties(cross_meta_assertions_${mode} PROPERTIES
            LABELS "frontend;feature.assertions;feature.meta;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_static_meta_bytes_checks_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-meta-bytes-checks-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/static_meta_bytes.cmake")
        set_tests_properties(cross_static_meta_bytes_checks_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.initializer;feature.syntax;arch.${assertion_arch};profile.${mode};diagnostics")
        foreach(byte_level O0 O2)
            set(byte_test cross_static_meta_bytes_${mode}_${byte_level})
            set(byte_output ${CMAKE_CURRENT_BINARY_DIR}/static-meta-bytes-${mode}-${byte_level})
            set(byte_source ${CMAKE_SOURCE_DIR}/test/frontend/static_meta_bytes.x)
            if(mode STREQUAL native OR mode STREQUAL custom)
                if(WIN32)
                    set(byte_host_abi ms_abi)
                else()
                    set(byte_host_abi sysv_abi)
                endif()
                set(byte_flags -${byte_level} -fno-eval-calls "-DHOST_ABI=\"${byte_host_abi}\"")
                if(mode STREQUAL custom)
                    list(APPEND byte_flags "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y" -mabi=odd_abi)
                endif()
                add_test(NAME ${byte_test} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${byte_source} -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${byte_output} -DENTRY=syntax_raw_entry -DEXPECTED=61 "-DCC_FLAGS=${byte_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            elseif(mode MATCHES "^mips64")
                set(byte_endian big)
                if(mode STREQUAL mips64el)
                    set(byte_endian little)
                endif()
                add_test(NAME ${byte_test} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DSOURCE=${byte_source}
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${byte_output} -DN64_ENDIAN=${byte_endian}
                    "-DEXTRA_FLAGS=-${byte_level} -fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            else()
                set(byte_endian b)
                if(mode STREQUAL mipsel)
                    set(byte_endian l)
                endif()
                add_test(NAME ${byte_test} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DSOURCE=${byte_source}
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                    -DOUTPUT=${byte_output} -DEXPECTED=P
                    -DMIPS_CPUS=vr4300 -DMIPS_ENDIANS=${byte_endian}
                    "-DCC_FLAGS=-${byte_level};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            endif()
            set_tests_properties(${byte_test} PROPERTIES
                LABELS "frontend;feature.meta;feature.initializer;feature.syntax;arch.${assertion_arch};profile.${mode};opt.${byte_level};check.runtime;backend.native"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
            if(mode MATCHES "^mips")
                if(mode MATCHES "el$")
                    set_property(TEST ${byte_test} APPEND PROPERTY LABELS endian.little)
                else()
                    set_property(TEST ${byte_test} APPEND PROPERTY LABELS endian.big)
                endif()
            endif()
        endforeach()
        add_test(NAME cross_meta_continuations_${mode}
                 COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-continuations-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_continuations.cmake")
        set_tests_properties(cross_meta_continuations_${mode} PROPERTIES
            LABELS "frontend;feature.evaluation;feature.meta;feature.syntax;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_attribute_ownership_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/attribute-ownership-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/attribute_ownership.cmake")
        set_tests_properties(cross_attribute_ownership_${mode} PROPERTIES
            LABELS "frontend;feature.attributes;feature.syntax;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_meta_attribute_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-attribute-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_attribute_checks.cmake")
        set_tests_properties(cross_meta_attribute_${mode} PROPERTIES
            LABELS "frontend;feature.attributes;feature.meta;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_syntax_relocation_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-relocation-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_relocation.cmake")
        set_tests_properties(cross_syntax_relocation_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.hygiene;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_syntax_fragment_order_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-fragment-order-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_fragment_order.cmake")
        set_tests_properties(cross_syntax_fragment_order_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.macro;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_callee_designators_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/callee-designators-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/callee_designators.cmake")
        set_tests_properties(cross_callee_designators_${mode} PROPERTIES
            LABELS "frontend;feature.calls;feature.syntax;feature.meta;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_syntax_validation_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-validation-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_validation.cmake")
        set_tests_properties(cross_syntax_validation_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.syntax_validation;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_meta_token_api_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-token-api-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_token_api.cmake")
        set_tests_properties(cross_meta_token_api_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.tokens;feature.syntax;arch.${assertion_arch};profile.${mode}")
        add_test(NAME cross_source_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/source-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/source_constraints.cmake")
        set_tests_properties(cross_source_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_control_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/control-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/control_constraints.cmake")
        set_tests_properties(cross_control_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.intrinsics;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_builtin_names_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DCPP=$<TARGET_FILE:cross_cpp>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/builtin-names-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/builtin_names.cmake")
        set_tests_properties(cross_builtin_names_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.intrinsics;feature.preprocessor;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_atomic_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomic-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/atomic_constraints.cmake")
        set_tests_properties(cross_atomic_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.atomics;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_atomic_query_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomic-queries-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/atomic_queries.cmake")
        set_tests_properties(cross_atomic_query_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.atomics;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_staging_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/staging-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/staging_constraints.cmake")
        set_tests_properties(cross_staging_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.evaluation;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_patch_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/patch_constraints.cmake")
        set_tests_properties(cross_patch_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.patch;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_static_relocation_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-relocation-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/static_relocation_constraints.cmake")
        set_tests_properties(cross_static_relocation_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.initializer;feature.relocation;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_address_space_source_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/address-space-source-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/address_space_source_constraints.cmake")
        set_tests_properties(cross_address_space_source_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.types;feature.address_space;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_raw_source_constraint_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc> -DMODE=${mode}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-source-constraints-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/raw_source_constraints.cmake")
        set_tests_properties(cross_raw_source_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.syntax;feature.raw;feature.patch;arch.${assertion_arch};profile.${mode};diagnostics")
        if(mode STREQUAL native OR mode STREQUAL custom)
            add_test(NAME cross_raw_label_constraint_${mode}
                COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                    -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-label-constraints-${mode}
                    -P "${CMAKE_SOURCE_DIR}/test/frontend/raw_label_constraints.cmake")
            set_tests_properties(cross_raw_label_constraint_${mode} PROPERTIES
                LABELS "frontend;feature.meta;feature.syntax;feature.raw;feature.labels;arch.x86_64;profile.${mode};diagnostics")
        endif()
        add_test(NAME cross_syntax_type_attribute_checks_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-type-attribute-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_type_attributes.cmake")
        set_tests_properties(cross_syntax_type_attribute_checks_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.attributes;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_syntax_resource_checks_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-resource-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_resources.cmake")
        set_tests_properties(cross_syntax_resource_checks_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.meta;feature.resources;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_syntax_enumerator_checks_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-enumerator-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_enumerators.cmake")
        set_tests_properties(cross_syntax_enumerator_checks_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.evaluation;feature.resources;feature.enum;feature.generic;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_pointer_normalization_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pointer-normalization-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/pointer_normalization.cmake")
        set_tests_properties(cross_pointer_normalization_${mode} PROPERTIES
            LABELS "frontend;feature.pointer;feature.evaluation;feature.resources;arch.${assertion_arch};profile.${mode};diagnostics")
        if(mode STREQUAL mips OR mode STREQUAL mips64)
            set_property(TEST cross_syntax_resource_checks_${mode} cross_syntax_enumerator_checks_${mode} cross_pointer_normalization_${mode}
                APPEND PROPERTY LABELS endian.big)
        else()
            set_property(TEST cross_syntax_resource_checks_${mode} cross_syntax_enumerator_checks_${mode} cross_pointer_normalization_${mode}
                APPEND PROPERTY LABELS endian.little)
        endif()
        if(mode STREQUAL mips OR mode STREQUAL mipsel)
            set_property(TEST cross_syntax_resource_checks_${mode} cross_syntax_enumerator_checks_${mode} cross_pointer_normalization_${mode}
                APPEND PROPERTY LABELS isa.r3000)
        endif()
        add_test(NAME cross_layout_resource_checks_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/layout-resource-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/layout_resources.cmake")
        set_tests_properties(cross_layout_resource_checks_${mode} PROPERTIES
            LABELS "frontend;feature.syntax;feature.meta;feature.resources;feature.records;arch.${assertion_arch};profile.${mode};diagnostics")
        if(mode STREQUAL mips OR mode STREQUAL mips64)
            set_property(TEST cross_layout_resource_checks_${mode} APPEND PROPERTY LABELS endian.big)
        else()
            set_property(TEST cross_layout_resource_checks_${mode} APPEND PROPERTY LABELS endian.little)
        endif()
        if(mode STREQUAL mips OR mode STREQUAL mipsel)
            set_property(TEST cross_layout_resource_checks_${mode} APPEND PROPERTY LABELS isa.r3000)
        endif()
        add_test(NAME cross_record_constraint_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/record-constraint-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/record_constraints.cmake")
        set_tests_properties(cross_record_constraint_${mode} PROPERTIES
            LABELS "frontend;feature.records;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_type_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/type-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/type_staging.cmake")
        set_tests_properties(cross_type_staging_${mode} PROPERTIES
            LABELS "frontend;feature.types;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_record_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/record-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/record_staging.cmake")
        set_tests_properties(cross_record_staging_${mode} PROPERTIES
            LABELS "frontend;feature.records;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_vector_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/vector_staging.cmake")
        set_tests_properties(cross_vector_staging_${mode} PROPERTIES
            LABELS "frontend;feature.vectors;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_generic_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/generic_staging.cmake")
        set_tests_properties(cross_generic_staging_${mode} PROPERTIES
            LABELS "frontend;feature.generic;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_generic_value_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-value-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/generic_value_staging.cmake")
        set_tests_properties(cross_generic_value_staging_${mode} PROPERTIES
            LABELS "frontend;feature.generic;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
        add_test(NAME cross_generic_initializer_staging_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-initializer-staging-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/generic_initializer_staging.cmake")
        set_tests_properties(cross_generic_initializer_staging_${mode} PROPERTIES
            LABELS "frontend;feature.generic;feature.initializer;feature.meta;feature.source_constraints;arch.${assertion_arch};profile.${mode};diagnostics")
    endforeach()
    add_test(NAME cross_meta_record_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-records
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_record_checks.cmake")
    set_tests_properties(cross_meta_record_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.meta;feature.records")
    add_test(NAME cross_meta_switch_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-switches
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_switch_checks.cmake")
    set_tests_properties(cross_meta_switch_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta;feature.control")
    add_test(NAME cross_meta_operator_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-operators
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_operator_checks.cmake")
    set_tests_properties(cross_meta_operator_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta;profile.custom")
    add_test(NAME cross_meta_conversion_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-conversions
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_conversion_checks.cmake")
    set_tests_properties(cross_meta_conversion_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta;profile.custom")
    add_test(NAME cross_meta_comparison_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-comparisons
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_comparison_checks.cmake")
    set_tests_properties(cross_meta_comparison_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta;profile.custom")
    add_test(NAME cross_meta_callable_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-callables
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_callable_checks.cmake")
    set_tests_properties(cross_meta_callable_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.meta;feature.calls;profile.custom")
    add_test(NAME cross_meta_name_checks
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                     -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-names
                     -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_name_checks.cmake")
    set_tests_properties(cross_meta_name_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta;profile.custom")
    foreach(mode shipped custom diagnostics)
        add_test(NAME cross_variadic_binding_checks_${mode}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -DMODE=${mode}
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadic-bindings-${mode}
                         -P "${CMAKE_SOURCE_DIR}/test/frontend/variadic_binding_checks.cmake")
        set_tests_properties(cross_variadic_binding_checks_${mode} PROPERTIES
            LABELS "frontend;feature.variadic;feature.syntax;feature.hygiene;arch.x86_64;profile.${mode}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_generic_group_runtime_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE_USE=${CMAKE_SOURCE_DIR}/test/language/generic_group_use.x
                         -DSOURCE_DEF=${CMAKE_SOURCE_DIR}/test/language/generic_group_def.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-group-${level}
                         -DENTRY=generic_group_entry
                         -DLEVEL=${level}
                         -P "${CMAKE_SOURCE_DIR}/test/language/generic_group_runtime.cmake")
        set_tests_properties(cross_generic_group_runtime_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic;opt.${level}")
        add_test(NAME cross_generic_group_custom_abi_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE_USE=${CMAKE_SOURCE_DIR}/test/language/generic_group_custom_use.x
                         -DSOURCE_DEF=${CMAKE_SOURCE_DIR}/test/language/generic_group_custom_def.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-group-custom-${level}
                         -DENTRY=generic_group_custom_entry
                         -DLEVEL=${level}
                         -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                         -P "${CMAKE_SOURCE_DIR}/test/language/generic_group_runtime.cmake")
        set_tests_properties(cross_generic_group_custom_abi_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.generic;feature.abi;opt.${level};profile.custom")
    endforeach()
    add_test(NAME cross_generic_group_errors
             COMMAND "${CMAKE_COMMAND}"
                     -DCC=$<TARGET_FILE:cross_cc>
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language
                     -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generic-group-errors
                     -P "${CMAKE_SOURCE_DIR}/test/language/generic_group_errors.cmake")
    set_tests_properties(cross_generic_group_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.generic")
    add_executable(cross_procedural_tokens_test test/frontend/procedural_tokens_test.cpp)
    target_link_libraries(cross_procedural_tokens_test PRIVATE cross_frontend)
    cross_component(cross_procedural_tokens_test)
    add_test(NAME cross_procedural_tokens COMMAND $<TARGET_FILE:cross_procedural_tokens_test>)
    set_tests_properties(cross_procedural_tokens PROPERTIES
        LABELS "check.unit;arch.independent;feature.procedural;feature.tokens")
    add_executable(cross_embed_identity_test test/frontend/embed_identity_test.cpp)
    target_link_libraries(cross_embed_identity_test PRIVATE cross_frontend)
    cross_component(cross_embed_identity_test)
    add_test(NAME cross_embed_identity
        COMMAND $<TARGET_FILE:cross_embed_identity_test>
                "${CMAKE_SOURCE_DIR}/test/frontend/preprocessor.x")
    set_tests_properties(cross_embed_identity PROPERTIES
        LABELS "check.unit;arch.independent;feature.preprocessor")

    add_executable(cross_address_space_types_test
                   test/frontend/address_space_types_test.cpp)
    target_link_libraries(cross_address_space_types_test PRIVATE cross_middle)
    cross_component(cross_address_space_types_test)
    add_test(NAME cross_address_space_types
             COMMAND $<TARGET_FILE:cross_address_space_types_test>)
    set_tests_properties(cross_address_space_types PROPERTIES
        LABELS "check.unit;arch.independent;feature.address_space")

    add_executable(cross_pointer_join_test test/frontend/pointer_join_test.cpp)
    target_link_libraries(cross_pointer_join_test PRIVATE cross_middle)
    cross_component(cross_pointer_join_test)
    add_test(NAME cross_pointer_join COMMAND $<TARGET_FILE:cross_pointer_join_test>)
    set_tests_properties(cross_pointer_join PROPERTIES
        LABELS "frontend;feature.types;feature.pointer;arch.independent;check.unit")

    add_executable(cross_callable_type_test
                   test/frontend/callable_type_test.cpp)
    target_link_libraries(cross_callable_type_test PRIVATE cross_middle)
    cross_component(cross_callable_type_test)
    add_test(NAME cross_callable_type
             COMMAND $<TARGET_FILE:cross_callable_type_test>)
    set_tests_properties(cross_callable_type PROPERTIES
        LABELS "check.unit;arch.independent;feature.calls")

    add_executable(cross_source_type_query_test test/frontend/source_type_query_test.cpp)
    target_link_libraries(cross_source_type_query_test PRIVATE cross_frontend)
    cross_component(cross_source_type_query_test)
    add_test(NAME cross_source_type_query COMMAND $<TARGET_FILE:cross_source_type_query_test>)
    set_tests_properties(cross_source_type_query PROPERTIES
        LABELS "frontend;feature.types;feature.generic;feature.meta;feature.evaluation;feature.resources;arch.independent;check.unit")

    add_executable(cross_callable_abi_test test/frontend/callable_abi_test.cpp)
    target_link_libraries(cross_callable_abi_test PRIVATE cross_frontend)
    cross_component(cross_callable_abi_test)
    add_test(NAME cross_callable_abi COMMAND $<TARGET_FILE:cross_callable_abi_test>)
    set_tests_properties(cross_callable_abi PROPERTIES
        LABELS "frontend;feature.types;feature.abi;feature.generic;feature.resources;arch.independent;check.unit")

    add_executable(cross_record_view_test test/frontend/record_view_test.cpp)
    target_link_libraries(cross_record_view_test PRIVATE cross_middle)
    cross_component(cross_record_view_test)
    foreach(mode native custom mips mipsel mips64 mips64el)
        set(view_triple x86_64-unknown-linux-gnu)
        set(view_abi sysv_abi)
        set(view_arch x86_64)
        if(mode MATCHES "^mips")
            set(view_triple ${mode}-unknown-elf)
            set(view_abi o32)
            set(view_arch mips)
            if(mode MATCHES "^mips64")
                set(view_abi n64)
            endif()
        endif()
        add_test(NAME cross_record_view_${mode}
            COMMAND cross_record_view_test ${view_triple} ${view_abi} ${mode})
        set_tests_properties(cross_record_view_${mode} PROPERTIES
            LABELS "frontend;feature.records;feature.meta;feature.initializer;arch.${view_arch};profile.${mode};check.unit")
    endforeach()

    add_executable(cross_nominal_identity_test
                   test/frontend/nominal_identity_test.cpp)
    target_link_libraries(cross_nominal_identity_test PRIVATE cross_middle)
    cross_component(cross_nominal_identity_test)
    add_test(NAME cross_nominal_identity
             COMMAND $<TARGET_FILE:cross_nominal_identity_test>)
    set_tests_properties(cross_nominal_identity PROPERTIES
        LABELS "check.unit;arch.independent;feature.nominal")

    add_executable(cross_options_test test/common/options_test.cpp)
    target_link_libraries(cross_options_test PRIVATE cross_targets)
    cross_component(cross_options_test)

    add_executable(cross_integer_semantics_test test/common/integer_semantics_test.cpp)
    cross_component(cross_integer_semantics_test)
    add_test(NAME cross_integer_semantics COMMAND $<TARGET_FILE:cross_integer_semantics_test>)
    set_tests_properties(cross_integer_semantics PROPERTIES
        LABELS "check.unit;arch.independent;feature.semantics")
    add_executable(cross_relocation_addend_test test/common/relocation_addend_test.cpp)
    cross_component(cross_relocation_addend_test)
    add_test(NAME cross_relocation_addend COMMAND $<TARGET_FILE:cross_relocation_addend_test>)
    set_tests_properties(cross_relocation_addend PROPERTIES
        LABELS "arch.independent;feature.relocation;check.unit")

    add_executable(cross_floating_semantics_test test/common/floating_semantics_test.cpp)
    target_link_libraries(cross_floating_semantics_test PRIVATE cross_common)
    cross_component(cross_floating_semantics_test)
    add_test(NAME cross_floating_semantics COMMAND $<TARGET_FILE:cross_floating_semantics_test>)
    set_tests_properties(cross_floating_semantics PROPERTIES
        LABELS "check.unit;arch.independent;feature.floating")

    add_test(NAME cross_semantic_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/language/semantics/runtime_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/semantic-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_semantic_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics")
    add_test(NAME cross_semantic_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language/semantics
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/semantic-errors
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/errors.cmake")
    set_tests_properties(cross_semantic_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.semantics")
    add_test(NAME cross_c_forms_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/c_forms.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/language/semantics/c_forms_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/c-forms-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/c_forms.cmake")
    set_tests_properties(cross_c_forms_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;arch.mips;backend.native;feature.semantics;feature.variadic;feature.parameters")
    add_test(NAME cross_out_arguments COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/out-arguments
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/out_arguments.cmake")
    set_tests_properties(cross_out_arguments PROPERTIES
        LABELS "check.compile;diagnostics;frontend;feature.parameters;arch.x86_64;arch.mips")
    foreach(mode native custom mips mipsel mips64 mips64el)
        if(mode MATCHES "^mips")
            set(out_arch mips)
        else()
            set(out_arch x86_64)
        endif()
        add_test(NAME cross_evaluation_outputs_${mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DMODE=${mode} -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evaluation-outputs-${mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/evaluation_outputs.cmake")
        set_tests_properties(cross_evaluation_outputs_${mode} PROPERTIES
            LABELS "frontend;feature.meta;feature.parameters;feature.source_constraints;arch.${out_arch};profile.${mode};diagnostics")
        if(mode STREQUAL mips OR mode STREQUAL mips64)
            set_property(TEST cross_evaluation_outputs_${mode} APPEND PROPERTY LABELS endian.big)
        else()
            set_property(TEST cross_evaluation_outputs_${mode} APPEND PROPERTY LABELS endian.little)
        endif()
        foreach(level O0 O2)
            add_test(NAME cross_out_definite_assignment_${mode}_${level}
                COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                    -DMODE=${mode} -DLEVEL=${level}
                    -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language/semantics
                    -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/out-definite-assignment-${mode}-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/out_definite_assignment.cmake")
            set_tests_properties(cross_out_definite_assignment_${mode}_${level} PROPERTIES
                LABELS "frontend;feature.parameters;feature.types;arch.${out_arch};profile.${mode};opt.${level};diagnostics")
            if(mode STREQUAL mips OR mode STREQUAL mips64)
                set_property(TEST cross_out_definite_assignment_${mode}_${level} APPEND PROPERTY LABELS endian.big)
            elseif(mode STREQUAL mipsel OR mode STREQUAL mips64el)
                set_property(TEST cross_out_definite_assignment_${mode}_${level} APPEND PROPERTY LABELS endian.little)
            endif()
            if(mode STREQUAL mips OR mode STREQUAL mipsel)
                set_property(TEST cross_out_definite_assignment_${mode}_${level} APPEND PROPERTY LABELS isa.r3000)
            endif()
        endforeach()
    endforeach()
    add_test(NAME cross_out_alias_assignment COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language/semantics
        -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/out-alias-assignment
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/out_alias.cmake")
    set_tests_properties(cross_out_alias_assignment PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;feature.semantics;profile.custom")
    add_test(NAME cross_procedural_scalar COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/frontend
        -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-scalar
        -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_scalar.cmake")
    set_tests_properties(cross_procedural_scalar PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.procedural;profile.custom")
    foreach(level IN ITEMS O0 O1 O2 O3 Os Oz)
        add_test(NAME cross_runtime_f80_promote_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/f80_promote.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/f80-promote-${level}
                -DENTRY=f80_promote_entry
                -DEXPECTED=15
                -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_f80_promote_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;opt.${level}")
    endforeach()
    foreach(level IN ITEMS O0 O2)
        add_test(NAME cross_runtime_relocatable_addresses_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/relocatable_addresses.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/relocatable-addresses-${level}
                -DENTRY=relocatable_address_entry
                -DEXPECTED=0
                -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_relocatable_addresses_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.object;feature.pointer;opt.${level}")
    endforeach()
    add_test(NAME cross_relocatable_addresses_targets
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/relocatable_addresses.x
            -DBAD_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/relocatable_addresses_bad.x
            -DBAD_CAST_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/relocatable_addresses_bad_cast.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/relocatable-addresses
            -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
            -P "${CMAKE_SOURCE_DIR}/test/middle/data/relocatable_addresses.cmake")
    set_tests_properties(cross_relocatable_addresses_targets PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;feature.object;feature.pointer")
    foreach(level IN ITEMS O0 O2)
        add_test(NAME cross_runtime_patch_relocatable_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_relocatable.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-relocatable-${level}
                -DENTRY=patch_relocatable_entry
                -DEXPECTED=0
                -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_patch_relocatable_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.patch;feature.object;opt.${level}")
    endforeach()
    add_test(NAME cross_patch_relocatable_targets
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_relocatable.x
            -DBAD_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_relocatable_shadow.x
            -DTLS_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_relocatable_tls.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-relocatable
            -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
            -P "${CMAKE_SOURCE_DIR}/test/middle/patch_relocatable.cmake")
    set_tests_properties(cross_patch_relocatable_targets PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;feature.patch;feature.object")
    if(WIN32)
        set(cross_patch_host_abi ms_abi)
    else()
        set(cross_patch_host_abi sysv_abi)
    endif()
    foreach(level O0 O2)
        foreach(profile native custom)
            set(cross_raw_patch_flags -${level} -fno-eval-calls "-DHOST_ABI=\"${cross_patch_host_abi}\"")
            if(profile STREQUAL custom)
                list(APPEND cross_raw_patch_flags "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y" -mabi=odd_abi)
            endif()
            add_test(NAME cross_raw_patch_values_${profile}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/patch_values.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-patch-values-${profile}-${level}
                    -DENTRY=raw_patch_entry -DEXPECTED=0
                    "-DCC_FLAGS=${cross_raw_patch_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_raw_patch_values_${profile}_${level} PROPERTIES
                LABELS "feature.patch;feature.raw;arch.x86_64;backend.native;check.runtime;profile.${profile};opt.${level}"
                TIMEOUT 30)
            add_test(NAME cross_raw_addressing_${profile}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/addressing.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-addressing-${profile}-${level}
                    -DENTRY=raw_address_entry -DEXPECTED=0
                    "-DCC_FLAGS=${cross_raw_patch_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_raw_addressing_${profile}_${level} PROPERTIES
                LABELS "feature.addressing;feature.raw;arch.x86_64;backend.native;check.runtime;profile.${profile};opt.${level}"
                TIMEOUT 30)
        endforeach()
    endforeach()
    add_test(NAME cross_semantic_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/mips.x
        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/semantics/mips_start.s
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/semantic-mips
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_semantic_mips PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.semantics")
    add_test(NAME cross_semantic_mandatory COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/mandatory_ops.x
        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
        -DENTRY=entry -DEXPECTED=0 -DCC_FLAGS=-O2
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/semantic-mandatory
        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_semantic_mandatory PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;opt.O2")
    add_test(NAME cross_feature_consistency COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/frontend
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/feature-consistency
        -P "${CMAKE_SOURCE_DIR}/test/frontend/feature_consistency.cmake")
    set_tests_properties(cross_feature_consistency PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.driver")
    add_test(NAME cross_address_spaces COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/frontend
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/address-spaces
                -P "${CMAKE_SOURCE_DIR}/test/frontend/address_spaces.cmake")
    set_tests_properties(cross_address_spaces PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.address_space")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_address_spaces_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/address_spaces.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/address-spaces-${level}
                    -DENTRY=address_space_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_address_spaces_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;frontend;feature.address_space;opt.${level}")
    endforeach()
    add_test(NAME cross_switch_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/switch/runtime.x
        -DRUNNER=${CMAKE_SOURCE_DIR}/test/language/switch/dynamic_entry.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/switch-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/switch/runtime.cmake")
    set_tests_properties(cross_switch_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.control")
    add_test(NAME cross_switch_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/switch/errors.x
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/switch-errors
        -P "${CMAKE_SOURCE_DIR}/test/language/switch/errors.cmake")
    set_tests_properties(cross_switch_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.control")
    foreach(level O0 O2 O3 Os)
        add_test(NAME cross_switch_nested_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/switch/nested.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/switch-nested-${level}
            -DENTRY=switch_nested_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_switch_nested_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.control;opt.${level}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_switch_nested_mips_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/switch/nested.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/switch/mips_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/switch-nested-mips-${level}
            -DEXPECTED=P -DCC_FLAGS=-${level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_switch_nested_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.control;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_noreturn_frames COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/noreturn/noreturn.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/noreturn
        -P "${CMAKE_SOURCE_DIR}/test/backend/noreturn/noreturn.cmake")
    set_tests_properties(cross_noreturn_frames PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_frame_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/frame-runtime
        -P "${CMAKE_SOURCE_DIR}/test/backend/frame/runtime.cmake")
    set_tests_properties(cross_frame_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_hard_register_unwind COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DAS=$<TARGET_FILE:cross_as> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/frame/hard_register_unwind.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hard-register-unwind
        -P "${CMAKE_SOURCE_DIR}/test/backend/frame/hard_register_unwind.cmake")
    set_tests_properties(cross_hard_register_unwind PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;feature.abi;feature.object")
    add_test(NAME cross_indirect_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/indirect-runtime
        -P "${CMAKE_SOURCE_DIR}/test/backend/indirect/runtime.cmake")
    set_tests_properties(cross_indirect_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.calls")
    add_test(NAME cross_indirect_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/indirect-mips
        -P "${CMAKE_SOURCE_DIR}/test/backend/indirect/mips.cmake")
    set_tests_properties(cross_indirect_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.calls")
    add_test(NAME cross_indirect_clobber_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/indirect/mips_clobber.x
        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/indirect/mips_clobber_start.s
        -DEXPECTED=P
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/indirect-clobber-mips
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_indirect_clobber_mips PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.calls;feature.abi")
    add_test(NAME cross_aggregate_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-mips
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/aggregate.cmake")
    set_tests_properties(cross_aggregate_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.records")
    add_test(NAME cross_private_record_result_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/private-record-result-mips
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/private_record_result.cmake")
    set_tests_properties(cross_private_record_result_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.abi;feature.records")
    add_test(NAME cross_abi_placement_diagnostics_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/placement_errors.x
        -DMODEL=${CMAKE_SOURCE_DIR}/test/target/mips/placement.y
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/abi-placement-errors-mips
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/placement_errors.cmake")
    set_tests_properties(cross_abi_placement_diagnostics_mips PROPERTIES
        LABELS "check.compile;diagnostics;arch.mips;backend.native;feature.abi;feature.model;isa.vr4300;endian.big;endian.little;opt.O0;opt.O2")
    add_test(NAME cross_indirect_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/indirect-errors
        -P "${CMAKE_SOURCE_DIR}/test/backend/indirect/errors.cmake")
    set_tests_properties(cross_indirect_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.calls")
    add_test(NAME cross_noreturn_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/noreturn/mips_noreturn.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/noreturn-mips
        -P "${CMAKE_SOURCE_DIR}/test/backend/noreturn/mips_noreturn.cmake")
    set_tests_properties(cross_noreturn_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.frame")
    foreach(level O1 O2 O3 Os)
        add_test(NAME cross_shrink_wrap_runtime_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DLEVEL=${level}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shrink-wrap-${level}
            -P "${CMAKE_SOURCE_DIR}/test/backend/native/shrink_wrap/runtime.cmake")
        set_tests_properties(cross_shrink_wrap_runtime_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.${level}")
    endforeach()
    foreach(arch x86_64 mips)
        add_test(NAME cross_shrink_wrap_assembly_${arch} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DARCH=${arch}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shrink-wrap-assembly-${arch}
            -P "${CMAKE_SOURCE_DIR}/test/backend/native/shrink_wrap/assembly.cmake")
        set_tests_properties(cross_shrink_wrap_assembly_${arch} PROPERTIES
            LABELS "check.compile;arch.${arch};backend.native;feature.frame;opt.O2")
    endforeach()
    # Each MIPS registration owns one CPU, byte order, and flag setting.
    foreach(cpu vr4300 r3000)
        foreach(endian big little)
            string(SUBSTRING ${endian} 0 1 endian_letter)
            foreach(mode shrink_wrap no_shrink_wrap shrink_wrap_frame_pointer)
                if(mode STREQUAL shrink_wrap_frame_pointer)
                    set(flags -fshrink-wrap -fno-omit-frame-pointer)
                else()
                    string(REPLACE "_" "-" flags -f${mode})
                endif()
                set(name cross_shrink_wrap_mips_${cpu}_${endian_letter}_${mode})
                add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/shrink_wrap/mips.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/native/shrink_wrap/mips_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shrink-wrap-mips-${cpu}-${endian_letter}-${mode}
                    -DMIPS_CPUS=${cpu} -DMIPS_ENDIANS=${endian_letter}
                    -DMIPS_SINGLE_CASE=ON -DEXPECTED=P "-DCC_FLAGS=${flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                set_tests_properties(${name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;feature.frame;profile.o32;opt.O2;isa.${cpu};endian.${endian}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()

    add_executable(cross_mir_verifier_test test/middle/mir_verifier_test.cpp)
    target_link_libraries(cross_mir_verifier_test PRIVATE cross_middle)
    cross_component(cross_mir_verifier_test)

    add_executable(cross_mir_analysis_test test/middle/mir_analysis_test.cpp)
    target_link_libraries(cross_mir_analysis_test PRIVATE cross_middle)
    cross_component(cross_mir_analysis_test)

    add_executable(cross_forwarding_blocks_test test/middle/forwarding_blocks_test.cpp)
    target_link_libraries(cross_forwarding_blocks_test PRIVATE cross_middle)
    cross_component(cross_forwarding_blocks_test)
    add_test(NAME cross_forwarding_blocks COMMAND $<TARGET_FILE:cross_forwarding_blocks_test>)
    set_tests_properties(cross_forwarding_blocks PROPERTIES
        LABELS "check.unit;arch.independent;middle;feature.ir;feature.optimization")

    add_executable(cross_initialization_test test/middle/initialization_test.cpp)
    target_link_libraries(cross_initialization_test PRIVATE cross_middle)
    cross_component(cross_initialization_test)
    add_test(NAME cross_initialization COMMAND $<TARGET_FILE:cross_initialization_test>)
    set_tests_properties(cross_initialization PROPERTIES
        LABELS "middle;feature.parameters;feature.types;arch.independent;check.unit")

    add_executable(cross_machine_ir_test test/middle/machine_ir_test.cpp)
    target_link_libraries(cross_machine_ir_test PRIVATE cross_middle)
    cross_component(cross_machine_ir_test)

    add_executable(cross_frame_ir_test test/middle/frame_ir_test.cpp)
    target_link_libraries(cross_frame_ir_test PRIVATE cross_middle)
    cross_component(cross_frame_ir_test)
    add_test(NAME cross_frame_ir COMMAND $<TARGET_FILE:cross_frame_ir_test>)
    set_tests_properties(cross_frame_ir PROPERTIES
        LABELS "check.unit;arch.independent;feature.frame;feature.ir")

    add_executable(cross_shrink_wrap_test test/middle/shrink_wrap_test.cpp)
    target_link_libraries(cross_shrink_wrap_test PRIVATE cross_middle)
    cross_component(cross_shrink_wrap_test)
    add_test(NAME cross_shrink_wrap_placement
             COMMAND $<TARGET_FILE:cross_shrink_wrap_test>)
    set_tests_properties(cross_shrink_wrap_placement PROPERTIES
        LABELS "check.unit;arch.independent;feature.frame;feature.ir")

    add_executable(cross_machine_transform_test
        test/backend/native/machine_transform_test.cpp)
    target_link_libraries(cross_machine_transform_test
        PRIVATE cross_native_backend)
    cross_component(cross_machine_transform_test)

    add_executable(cross_abi_rules_test test/model/abi_rules_test.cpp)
    target_link_libraries(cross_abi_rules_test PRIVATE cross_native_backend)
    cross_component(cross_abi_rules_test)

    add_executable(cross_abi_layouts_test test/model/abi_layouts.cpp)
    target_link_libraries(cross_abi_layouts_test PRIVATE cross_targets)
    cross_component(cross_abi_layouts_test)

    add_executable(cross_manual_abi_plan_test test/backend/abi/manual_abi_plan_test.cpp)
    target_link_libraries(cross_manual_abi_plan_test PRIVATE cross_native_backend)
    cross_component(cross_manual_abi_plan_test)

    add_test(NAME cross_help COMMAND $<TARGET_FILE:cross_cc> --help)
    set_tests_properties(cross_help PROPERTIES
        LABELS "check.compile;arch.independent;feature.driver")
    add_test(NAME cross_options COMMAND $<TARGET_FILE:cross_options_test>)
    set_tests_properties(cross_options PROPERTIES
        LABELS "check.unit;arch.independent;feature.driver")
    add_test(NAME cross_mir_analysis
             COMMAND $<TARGET_FILE:cross_mir_analysis_test>)
    set_tests_properties(cross_mir_analysis PROPERTIES
        LABELS "check.unit;arch.independent;feature.ir")
    add_test(NAME cross_machine_ir
             COMMAND $<TARGET_FILE:cross_machine_ir_test>)
    set_tests_properties(cross_machine_ir PROPERTIES
        LABELS "check.unit;arch.independent;feature.ir")
    add_test(NAME cross_machine_transform
             COMMAND $<TARGET_FILE:cross_machine_transform_test>)
    set_tests_properties(cross_machine_transform PROPERTIES
        LABELS "check.unit;arch.independent;feature.ir")
    add_test(NAME cross_version COMMAND $<TARGET_FILE:cross_cc> --version)
    set_tests_properties(cross_version PROPERTIES
        LABELS "check.compile;arch.independent;feature.driver")
    add_test(NAME cross_targets COMMAND $<TARGET_FILE:cross_cc> --print-targets)
    set_tests_properties(cross_targets PROPERTIES
        LABELS "check.compile;arch.independent;feature.driver")
    add_test(NAME cross_abis COMMAND $<TARGET_FILE:cross_cc> --print-abis)
    set_tests_properties(cross_abis PROPERTIES
        LABELS "check.compile;arch.independent;feature.abi")
    add_test(NAME cross_mips_vr4300_o32
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-o32
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.cmake")
    set_tests_properties(cross_mips_vr4300_o32 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.codegen")
    add_test(NAME cross_mips_optimization_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/optimization/codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-optimization-codegen
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/optimization/codegen.cmake")
    set_tests_properties(cross_mips_optimization_codegen PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.optimization")
    # GNU as accepts the assembly of every MIPS profile and encodes it as
    # cc -c does.
    add_test(NAME cross_mips_gnu_as
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                "-DSOURCES=${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.x;${CMAKE_SOURCE_DIR}/test/language/semantics/mips.x;${CMAKE_SOURCE_DIR}/test/language/semantics/subscripts.x;${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x;${CMAKE_SOURCE_DIR}/test/target/mips/vla.x;${CMAKE_SOURCE_DIR}/test/target/mips/preserved_width.x;${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics.x;${CMAKE_SOURCE_DIR}/test/target/mips/fpr_allocation_single.x;${CMAKE_SOURCE_DIR}/test/target/mips/instruction_builtins.x"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-gnu-as/output
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.cmake")
    set_tests_properties(cross_mips_gnu_as PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;endian.big;endian.little")
    add_test(NAME cross_mips_gnu_as_small_data
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                "-DSOURCES=${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.x;${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x;${CMAKE_SOURCE_DIR}/test/language/semantics/subscripts.x"
                "-DVARIANTS=sdata-O0|-O0 -mdata-bases=mips-gp;sdata-O2|-O2 -g -fdata-sections -mdata-bases=mips-gp"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-gnu-as-small-data/output
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.cmake")
    set_tests_properties(cross_mips_gnu_as_small_data PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;endian.big;endian.little;feature.small_data;opt.O0;opt.O2")
    # The Cross assembler: encodings, relocations, DWARF tables, diagnostics,
    # the as tool, and linker acceptance of cc -c objects.
    foreach(name encodings relocations directives dwarf table)
        add_test(NAME cross_mips_assembler_${name}
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/assembler/${name}.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler/${name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/${name}.cmake")
        set_tests_properties(cross_mips_assembler_${name} PROPERTIES
            LABELS "feature.assembler;arch.mips;check.compile;endian.big;endian.little")
    endforeach()
    # GNU as compatibility: one source per feature, its .text words in the
    # comments, compared with GNU as when installed. ARGN: extra -D options.
    function(cross_mips_assembler_words name source options gnu_options labels)
        add_test(NAME cross_mips_assembler_${name}
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/assembler/${source}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler/${name}
                    "-DOPTIONS=${options}" "-DGNU_OPTIONS=${gnu_options}" ${ARGN}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/words.cmake")
        set_tests_properties(cross_mips_assembler_${name} PROPERTIES
            LABELS "feature.assembler;arch.mips;check.compile;${labels}")
    endfunction()
    set(mips_assembler_checks ${CMAKE_SOURCE_DIR}/test/target/mips/assembler)
    cross_mips_assembler_words(fpr_names fpr_names.s
        "-target mips-unknown-elf -march=vr4300 -mabi=32" "-EB -march=vr4300 -mabi=32"
        "endian.big" -DCHECK=${mips_assembler_checks}/fpr_names_check.cmake)
    cross_mips_assembler_words(subsections subsections.s
        "-target mips-unknown-elf" "-EB -march=mips1 -mabi=32"
        "endian.big" -DCHECK=${mips_assembler_checks}/subsections_check.cmake)
    cross_mips_assembler_words(deferred deferred.s
        "-target mips-unknown-elf" "-EB -march=mips1 -mabi=32"
        "endian.big" -DCHECK=${mips_assembler_checks}/deferred_check.cmake)
    cross_mips_assembler_words(instructions_mips32r2 instructions_r2.s
        "-target mips-unknown-elf -march=mips32r2" "-EB -march=mips32r2 -mabi=32" "endian.big")
    cross_mips_assembler_words(instructions_vr4300 instructions_mips3.s
        "-target mips-unknown-elf -march=vr4300" "-EB -march=vr4300 -mabi=32" "endian.big")
    cross_mips_assembler_words(instructions_allegrex instructions_allegrex.s
        "-target mipsallegrexel-sony-psp-elf -march=allegrex" "-EL -march=allegrex"
        "endian.little" -DORDER=little -DGNU_WORDS=1)
    cross_mips_assembler_words(conventions conventions.s
        "-target mips-unknown-elf -march=vr4300 -mabi=32 -mgp32 -O2"
        "-EB -O2 -march=vr4300 -mabi=32 -mgp32"
        "endian.big" -DCHECK=${mips_assembler_checks}/conventions_check.cmake)
    # Release 6: its instructions and the forms it re-encodes, and its
    # branches, whose PC-relative relocations GNU as keeps in their section.
    cross_mips_assembler_words(instructions_mips32r6 instructions_r6.s
        "-target mips-unknown-elf -march=mips32r6" "-EB -march=mips32r6 -mabi=32"
        "endian.big;isa.mips32r6")
    cross_mips_assembler_words(instructions_mips64r6 instructions_r6_64.s
        "-target mips64-unknown-elf -march=mips64r6 -mabi=n64" "-EB -march=mips64r6 -mabi=64"
        "endian.big;isa.mips64r6")
    cross_mips_assembler_words(branches_mips32r6 branches_r6.s
        "-target mips-unknown-elf -march=mips32r6" "-EB -march=mips32r6 -mabi=32"
        "endian.big;isa.mips32r6")
    add_test(NAME cross_mips_assembler_release6
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-release6/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/release6.cmake")
    set_tests_properties(cross_mips_assembler_release6 PROPERTIES
        LABELS "feature.assembler;arch.mips;check.compile;diagnostics;isa.mips32r6;isa.mips64r6;isa.mips32r2;isa.mips64r2;endian.big")
    foreach(name module reorder data_relocations)
        add_test(NAME cross_mips_assembler_${name}
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler/${name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/${name}.cmake")
        set_tests_properties(cross_mips_assembler_${name} PROPERTIES
            LABELS "feature.assembler;arch.mips;check.compile;endian.big")
    endforeach()
    add_test(NAME cross_mips_assembler_isa
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-isa/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/isa.cmake")
    set_tests_properties(cross_mips_assembler_isa PROPERTIES
        LABELS "feature.assembler;arch.mips;check.compile;diagnostics")
    add_test(NAME cross_mips_assembler_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-diagnostics/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/diagnostics.cmake")
    set_tests_properties(cross_mips_assembler_diagnostics PROPERTIES
        LABELS "feature.assembler;arch.mips;diagnostics")
    add_test(NAME cross_mips_assembler_tool
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-tool
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/tool.cmake")
    set_tests_properties(cross_mips_assembler_tool PROPERTIES
        LABELS "feature.assembler;arch.mips;check.compile;endian.big;endian.little")
    add_test(NAME cross_mips_assembler_pic
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-pic/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/pic.cmake")
    set_tests_properties(cross_mips_assembler_pic PROPERTIES
        LABELS "feature.assembler;feature.pic;feature.tls;arch.mips;check.compile;diagnostics;endian.big;endian.little")
    add_test(NAME cross_mips_assembler_linkers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                "-DSOURCES=${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.x;${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x;${CMAKE_SOURCE_DIR}/test/target/mips/vla.x"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-assembler-linkers/output
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/assembler/linkers.cmake")
    set_tests_properties(cross_mips_assembler_linkers PROPERTIES
        LABELS "feature.assembler;arch.mips;backend.native;check.compile;endian.big;endian.little")
    # The Cross ld: relocations, the --just-symbols KSEG0 case, linker
    # scripts (overlays, memory regions, fills and section types, inputs
    # and INSERT, expressions and empty sections), archives, options and
    # diagnostics, relocatable output, and images that GNU ld links the
    # same.
    foreach(linker_test relocations just_symbols scripts overlays regions fills inputs expressions
            archives tool relocatable)
        add_test(NAME cross_mips_linker_${linker_test}
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DLD=$<TARGET_FILE:cross_ld>
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-linker/${linker_test}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/linker/${linker_test}.cmake")
        set_tests_properties(cross_mips_linker_${linker_test} PROPERTIES
            LABELS "feature.linker;arch.mips;check.compile;endian.big;endian.little")
    endforeach()
    # Static links of abicalls and TLS objects: the GOT, $gp, and the TLS
    # segment.
    add_test(NAME cross_mips_linker_abicalls
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DLD=$<TARGET_FILE:cross_ld>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-linker/abicalls
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/linker/abicalls.cmake")
    set_tests_properties(cross_mips_linker_abicalls PROPERTIES
        LABELS "feature.linker;feature.pic;feature.tls;arch.mips;check.compile;diagnostics;endian.big;endian.little;profile.n64")
    add_test(NAME cross_mips_linker_gnu_ld
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DLD=$<TARGET_FILE:cross_ld>
                "-DSOURCES=${CMAKE_SOURCE_DIR}/test/target/mips/gnu_as.x;${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x;${CMAKE_SOURCE_DIR}/test/target/mips/vla.x"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-linker/gnu-ld
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/linker/gnu_ld.cmake")
    set_tests_properties(cross_mips_linker_gnu_ld PROPERTIES
        LABELS "feature.linker;arch.mips;backend.native;check.compile;endian.big;endian.little")
    # .eh_frame editing and .eh_frame_hdr, with objects from cc -funwind-tables.
    add_test(NAME cross_mips_linker_eh_frame
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DAS=$<TARGET_FILE:cross_as>
                -DLD=$<TARGET_FILE:cross_ld>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-linker/eh-frame
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/linker/eh_frame.cmake")
    set_tests_properties(cross_mips_linker_eh_frame PROPERTIES
        LABELS "feature.linker;arch.mips;backend.native;check.compile;endian.big")
    add_test(NAME cross_mips_allegrex
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DMIPS1_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/mips1_load.x
                -DF64_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/single_float_error.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-allegrex
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.cmake")
    set_tests_properties(cross_mips_allegrex PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.codegen")
    # Allegrex code run in PPSSPP, linked by LLD and by the Cross ld.
    foreach(psp_linker lld ld)
        if(psp_linker STREQUAL ld)
            set(psp_test cross_mips_allegrex_runtime_ld)
            set(psp_ld "-DLD=$<TARGET_FILE:cross_ld>")
            set(psp_labels ";feature.linker")
        else()
            set(psp_test cross_mips_allegrex_runtime)
            set(psp_ld "")
            set(psp_labels "")
        endif()
        add_test(NAME ${psp_test}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> ${psp_ld}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.x
                    -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                    -DBIT_SOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics.x
                    -DBUILTIN_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/psp/builtins.x
                    -DRUNTIME=${CMAKE_SOURCE_DIR}/test/target/mips/psp/runtime.x
                    -DMODULE_INFO=${CMAKE_SOURCE_DIR}/test/target/mips/psp/module_info.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/psp/link.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-allegrex-runtime-${psp_linker}/image
                    -DPPSSPP=${CROSS_PPSSPP_HEADLESS}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/psp/runtime.cmake")
        set_tests_properties(${psp_test} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.codegen;feature.intrinsics${psp_labels};isa.allegrex;endian.little;opt.O2"
            SKIP_REGULAR_EXPRESSION "skipping PSP runtime test")
    endforeach()
    add_test(NAME cross_mips_vr4300_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DMIPS1_FLOAT_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/mips1_float.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-runtime
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/runtime.cmake")
    set_tests_properties(cross_mips_vr4300_runtime PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen")
    # The same image with its startup and wrapper assembled by Cross as.
    add_test(NAME cross_mips_vr4300_runtime_as
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DAS=$<TARGET_FILE:cross_as>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DMIPS1_FLOAT_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/mips1_float.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-runtime-as
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/runtime.cmake")
    set_tests_properties(cross_mips_vr4300_runtime_as PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen;feature.assembler")
    # The same image assembled by Cross as and linked by Cross ld.
    add_test(NAME cross_mips_vr4300_runtime_ld
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DAS=$<TARGET_FILE:cross_as>
                -DLD=$<TARGET_FILE:cross_ld>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vr4300_o32.x
                -DMEMORY_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/memory_atomic.x
                -DPAIR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/i64_pair.x
                -DMIPS1_FLOAT_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/mips1_float.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vr4300-runtime-ld
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/runtime.cmake")
    set_tests_properties(cross_mips_vr4300_runtime_ld PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen;feature.assembler;feature.linker")
    # Machine-instruction built-ins, coprocessor 0 hazards, and naked
    # functions on the VR4300.
    add_test(NAME cross_mips_machine_instructions COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/machine_instructions_codegen.x
        -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/machine_instructions_errors.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-machine-instructions
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/machine_instructions.cmake")
    set_tests_properties(cross_mips_machine_instructions PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.raw;diagnostics;isa.vr4300;isa.mips32r2;isa.mips64r2;isa.mips32r6;isa.mips64r6")
    # Naked functions: exception returns, coprocessor 0 selects, exception
    # instructions, and their diagnostics; raw branches, and managed calls to
    # naked routines with register interfaces, under o32 and n64.
    add_test(NAME cross_mips_naked_forms COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/naked_forms.x
        -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/naked_forms_errors.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-naked-forms
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/naked_forms.cmake")
    set_tests_properties(cross_mips_naked_forms PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.raw;diagnostics;isa.r3000;isa.mips32;isa.vr4300;opt.O0;opt.O2")
    foreach(level O0 O2)
        foreach(cpu vr4300 r3000)
            set(name cross_mips_naked_calls_${cpu}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/naked_calls.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-naked-calls-${cpu}-${level}
                -DMIPS_CPUS=${cpu} -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.raw;feature.calls;profile.o32;isa.${cpu};endian.big;endian.little;opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
        foreach(endian big little)
            set(name cross_mips_naked_calls_n64_${endian}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/naked_calls.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DN64_ENDIAN=${endian} "-DEXTRA_FLAGS=-${level} -fno-eval-calls"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-naked-calls-n64-${endian}-${level}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.raw;feature.calls;profile.n64;isa.mips64;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    foreach(level O0 O2)
        set(name cross_mips_machine_instructions_runtime_${level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/machine_instructions.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/machine_instructions_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-machine-instructions-${level}
            -DMIPS_CPUS=vr4300 -DEXPECTED=11111111111111 "-DCC_FLAGS=-${level}"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.raw;isa.vr4300;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # The built-ins that every instruction table entry gives: their code,
    # availability, listing, and diagnostics, and their results under QEMU
    # on MIPS I and MIPS III, MIPS32 Release 2, and MIPS32 Release 6 CPUs.
    add_test(NAME cross_mips_instruction_builtins COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/instruction_builtins.x
        -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/instruction_builtins_errors.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-instruction-builtins/case
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/instruction_builtins.cmake")
    set_tests_properties(cross_mips_instruction_builtins PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.raw;feature.model;diagnostics;isa.r3000;isa.vr4300;isa.mips32r2;isa.allegrex;isa.mips32r6;isa.mips64r6;opt.O2")
    foreach(level O0 O2)
        foreach(builtin_case "vr4300|vr4300,r3000|R4000|OFF|-fno-eval-calls"
                             "mips32r2|mips32r2|MIPS64R2-generic|OFF|-fno-eval-calls,-mfp32"
                             "mips32r6|mips32r6|mips32r6-generic|ON|-fno-eval-calls")
            string(REPLACE "|" ";" builtin_fields "${builtin_case}")
            list(GET builtin_fields 0 builtin_name)
            list(GET builtin_fields 1 builtin_cpus)
            list(GET builtin_fields 2 builtin_qemu)
            list(GET builtin_fields 3 builtin_release6)
            list(GET builtin_fields 4 builtin_options)
            string(REPLACE "," ";" builtin_cpus "${builtin_cpus}")
            string(REPLACE "," ";" builtin_options "${builtin_options}")
            set(name cross_mips_instruction_builtins_runtime_${builtin_name}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/instruction_builtins_runtime.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-instruction-builtins-${builtin_name}-${level}
                -DEXPECTED=P "-DCC_FLAGS=-${level};${builtin_options}"
                "-DMIPS_CPUS=${builtin_cpus}" -DQEMU_CPU=${builtin_qemu}
                -DMIPS_RELEASE6=${builtin_release6}
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set(builtin_labels "check.runtime;arch.mips;backend.native;feature.raw;endian.big;endian.little;opt.${level}")
            foreach(builtin_cpu IN LISTS builtin_cpus)
                list(APPEND builtin_labels "isa.${builtin_cpu}")
            endforeach()
            set_tests_properties(${name} PROPERTIES LABELS "${builtin_labels}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # o32 record arguments start at a slot of their alignment; Cross and a
    # Clang-compiled C reference call each other.
    foreach(level O0 O2)
        set(name cross_mips_o32_records_${level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/o32_records.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/o32_records_start.s
            -DREFERENCE_C=${CMAKE_SOURCE_DIR}/test/target/mips/o32_records.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-o32-records-${level}
            -DEXPECTED=1 "-DCC_FLAGS=-${level}"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.records;profile.o32;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        # Instances of generic records are classified like the equivalent
        # written C structures.
        set(name cross_mips_o32_generic_records_${level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/o32_generic_records.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/o32_generic_records_start.s
            -DREFERENCE_C=${CMAKE_SOURCE_DIR}/test/target/mips/o32_generic_records.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-o32-generic-records-${level}
            -DEXPECTED=1 "-DCC_FLAGS=-${level}"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.records;feature.generic;profile.o32;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_mips_long_calls COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/long_calls.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-long-calls
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/long_calls.cmake")
    set_tests_properties(cross_mips_long_calls PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.calls;opt.O2")
    add_test(NAME cross_mips_small_data COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -DGIMPLE_TEXT=${CROSS_ENABLE_GCC_GIMPLE_TEXT}
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/small_data.cmake")
    set_tests_properties(cross_mips_small_data PROPERTIES
        LABELS "check.compile;diagnostics;arch.mips;endian.big;backend.native;backend.llvm;backend.gcc;feature.small_data;feature.abi;opt.O0;opt.O2;opt.Oz;profile.custom")
    # Small data through $gp at run time: VR4300 and R3000 o32 and n64 in
    # both byte orders, with a Cross partner unit or a GCC -G8 one.
    foreach(small_data_level O0 O2)
        add_test(NAME cross_mips_small_data_runtime_${small_data_level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
            -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_start.s
            -DMIPS_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data.ld
            "-DCC_FLAGS=-${small_data_level};-mdata-bases=mips-gp"
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-runtime-${small_data_level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_small_data_runtime_${small_data_level} PROPERTIES
            LABELS "check.runtime;arch.mips;endian.big;endian.little;backend.native;feature.small_data;opt.${small_data_level}")
        foreach(small_data_endian big little)
            set(small_data_test cross_mips_small_data_n64_${small_data_endian}_${small_data_level})
            add_test(NAME ${small_data_test} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
                -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_n64_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data-n64.ld
                -DN64_ENDIAN=${small_data_endian}
                "-DEXTRA_FLAGS=-${small_data_level} -mdata-bases=mips-gp"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-n64-${small_data_endian}-${small_data_level}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${small_data_test} PROPERTIES
                LABELS "check.runtime;arch.mips;endian.${small_data_endian};backend.native;feature.small_data;profile.n64;opt.${small_data_level}")
        endforeach()
    endforeach()
    add_test(NAME cross_mips_small_data_gcc COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
        -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.c
        -DGCC_FLAGS=-G8
        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_start.s
        -DMIPS_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data.ld
        -DCC_FLAGS=-mdata-bases=mips-gp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-gcc
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_mips_small_data_gcc PROPERTIES
        LABELS "check.runtime;arch.mips;endian.big;endian.little;backend.native;feature.small_data;feature.interop;opt.O2")
    # GNU as objects of GCC -G8 output and Cross objects, linked by Cross ld,
    # in o32 (which LLD rejects) and n64.
    add_test(NAME cross_mips_small_data_gcc_ld COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DLD=$<TARGET_FILE:cross_ld>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
        -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.c
        -DGCC_FLAGS=-G8
        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_start.s
        -DMIPS_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data.ld
        -DCC_FLAGS=-mdata-bases=mips-gp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-gcc-ld
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_mips_small_data_gcc_ld PROPERTIES
        LABELS "check.runtime;arch.mips;endian.big;endian.little;backend.native;feature.small_data;feature.interop;feature.linker;opt.O2")
    add_test(NAME cross_mips_small_data_n64_gcc_ld COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DLD=$<TARGET_FILE:cross_ld>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
        -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.c
        -DGCC_FLAGS=-G8
        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_n64_start.s
        -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data-n64.ld
        -DN64_ENDIAN=big
        -DEXTRA_FLAGS=-mdata-bases=mips-gp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-n64-gcc-ld
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
    set_tests_properties(cross_mips_small_data_n64_gcc_ld PROPERTIES
        LABELS "check.runtime;arch.mips;endian.big;backend.native;feature.small_data;feature.interop;feature.linker;profile.n64;opt.O2")
    foreach(small_data_endian big little)
        add_test(NAME cross_mips_small_data_n64_gcc_${small_data_endian} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_runtime.x
            -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_partner.c
            -DGCC_FLAGS=-G8
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/small_data_n64_start.s
            -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/small-data-n64.ld
            -DN64_ENDIAN=${small_data_endian}
            -DEXTRA_FLAGS=-mdata-bases=mips-gp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-small-data-n64-gcc-${small_data_endian}
            -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
        set_tests_properties(cross_mips_small_data_n64_gcc_${small_data_endian} PROPERTIES
            LABELS "check.runtime;arch.mips;endian.${small_data_endian};backend.native;feature.small_data;feature.interop;profile.n64;opt.O2")
    endforeach()
    # -mabicalls and TLS code: assembly, objects that GNU as encodes alike,
    # pic_call_clobbers in a user model, and diagnostics.
    add_test(NAME cross_mips_abicalls COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/abicalls_runtime.x
        -DMODEL=${CMAKE_SOURCE_DIR}/test/target/mips/abicalls.y
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-abicalls
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/abicalls.cmake")
    set_tests_properties(cross_mips_abicalls PROPERTIES
        LABELS "check.compile;diagnostics;arch.mips;endian.big;endian.little;backend.native;feature.pic;feature.tls;feature.abi;profile.n64;profile.custom;opt.O2")
    # The same code at run time: VR4300 and R3000 o32 and x32 code in both
    # byte orders on an R4000, whose startup emulates `rdhwr`, in PIC, PIE,
    # -mno-shared, and absolute code, with a Cross partner or a GCC
    # -mabicalls one, linked by LLD, the Cross ld, and GNU ld.
    set(abicalls_dir ${CMAKE_SOURCE_DIR}/test/target/mips)
    foreach(abicalls_case
            "pic|-fPIC|-mabicalls|"
            "pic_O0|-O0 -fPIC|-mabicalls|"
            "x32|-mabi=x32 -fPIC|-mabicalls|"
            "pie_ld|-fpie|-mabicalls|$<TARGET_FILE:cross_ld>"
            "no_shared|-mabicalls -mno-shared|-mabicalls -mno-shared|"
            "absolute|||")
        string(REPLACE "|" ";" abicalls_fields "${abicalls_case}")
        list(GET abicalls_fields 0 abicalls_name)
        list(GET abicalls_fields 1 abicalls_flags)
        list(GET abicalls_fields 2 abicalls_as_flags)
        list(GET abicalls_fields 3 abicalls_ld)
        string(REPLACE " " ";" abicalls_flags "${abicalls_flags}")
        string(REPLACE " " ";" abicalls_as_flags "${abicalls_as_flags}")
        add_test(NAME cross_mips_abicalls_runtime_${abicalls_name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DLD=${abicalls_ld}
            -DSOURCE=${abicalls_dir}/abicalls_runtime.x
            -DCALLEE_SOURCE=${abicalls_dir}/abicalls_partner.x
            -DSTARTUP=${abicalls_dir}/abicalls_start.s
            -DSTARTUP_AS=$<TARGET_FILE:cross_as>
            "-DSTARTUP_AS_FLAGS=${abicalls_as_flags}"
            -DMIPS_LINKER=${abicalls_dir}/abicalls.ld
            "-DCC_FLAGS=${abicalls_flags}"
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-abicalls-runtime-${abicalls_name}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set(abicalls_level O2)
        if(abicalls_flags MATCHES "-O0")
            set(abicalls_level O0)
        endif()
        set_tests_properties(cross_mips_abicalls_runtime_${abicalls_name} PROPERTIES
            LABELS "check.runtime;arch.mips;endian.big;endian.little;backend.native;feature.pic;feature.tls;feature.linker;opt.${abicalls_level}")
    endforeach()
    foreach(abicalls_ld gnu cross)
        set(abicalls_test cross_mips_abicalls_runtime_gcc_${abicalls_ld})
        set(abicalls_linker "")
        if(abicalls_ld STREQUAL "cross")
            set(abicalls_linker $<TARGET_FILE:cross_ld>)
        endif()
        add_test(NAME ${abicalls_test} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DLD=${abicalls_linker}
            -DSOURCE=${abicalls_dir}/abicalls_runtime.x
            -DGCC_SOURCE=${abicalls_dir}/abicalls_partner.c
            "-DGCC_FLAGS=-mabicalls;-fPIC;-G0;-ftls-model=initial-exec"
            -DSTARTUP=${abicalls_dir}/abicalls_start.s
            -DSTARTUP_AS=$<TARGET_FILE:cross_as>
            -DSTARTUP_AS_FLAGS=-mabicalls
            -DMIPS_LINKER=${abicalls_dir}/abicalls.ld
            -DCC_FLAGS=-fPIC
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-abicalls-runtime-gcc-${abicalls_ld}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${abicalls_test} PROPERTIES
            LABELS "check.runtime;arch.mips;endian.big;endian.little;backend.native;feature.pic;feature.tls;feature.interop;feature.linker;opt.O2")
    endforeach()
    # n64: MIPS III code on an R4000 that emulates `rdhwr`, and MIPS64 R2
    # code on a Loongson-3A4000, which reads UserLocal, with a Cross or GCC
    # partner, linked by LLD or the Cross ld.
    foreach(abicalls_case
            "pic_big|big|-fPIC -march=mips3|R4000||"
            "pic_little|little|-fPIC -march=mips3|R4000||"
            "pic_O0|big|-O0 -fPIC -march=mips3|R4000||"
            "pic_ld|big|-fPIC -march=mips3|R4000|cross|"
            "no_shared|little|-mabicalls -mno-shared -march=mips3|R4000||"
            "absolute|big|-march=mips3|R4000||"
            "r2|little|-fPIC -march=mips64r2|Loongson-3A4000||"
            "gcc|little|-fPIC -march=mips64r2|Loongson-3A4000||gcc"
            "gcc_ld|big|-fPIC -march=mips64r2|Loongson-3A4000|cross|gcc")
        string(REPLACE "|" ";" abicalls_fields "${abicalls_case}")
        list(GET abicalls_fields 0 abicalls_name)
        list(GET abicalls_fields 1 abicalls_endian)
        list(GET abicalls_fields 2 abicalls_flags)
        list(GET abicalls_fields 3 abicalls_cpu)
        list(GET abicalls_fields 4 abicalls_ld)
        list(GET abicalls_fields 5 abicalls_partner)
        set(abicalls_linker "")
        set(abicalls_partner_args -DCALLEE_SOURCE=${abicalls_dir}/abicalls_partner.x)
        set(abicalls_level O2)
        if(abicalls_flags MATCHES "-O0")
            set(abicalls_level O0)
        endif()
        set(abicalls_labels "check.runtime;arch.mips;endian.${abicalls_endian};backend.native;feature.pic;feature.tls;profile.n64;opt.${abicalls_level}")
        if(abicalls_ld STREQUAL "cross")
            set(abicalls_linker $<TARGET_FILE:cross_ld>)
            string(APPEND abicalls_labels ";feature.linker")
        endif()
        if(abicalls_partner STREQUAL "gcc")
            set(abicalls_partner_args -DGCC_SOURCE=${abicalls_dir}/abicalls_partner.c
                "-DGCC_FLAGS=-mabicalls -fPIC -G0 -ftls-model=initial-exec")
            string(APPEND abicalls_labels ";feature.interop")
        endif()
        add_test(NAME cross_mips_abicalls_n64_${abicalls_name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DLD=${abicalls_linker}
            -DSOURCE=${abicalls_dir}/abicalls_runtime.x
            ${abicalls_partner_args}
            -DSTARTUP=${abicalls_dir}/abicalls_n64_start.s
            -DLINKER=${abicalls_dir}/abicalls-n64.ld
            -DN64_ENDIAN=${abicalls_endian}
            "-DEXTRA_FLAGS=${abicalls_flags}"
            -DN64_QEMU_CPU=${abicalls_cpu}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-abicalls-n64-${abicalls_name}
            -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
        set_tests_properties(cross_mips_abicalls_n64_${abicalls_name} PROPERTIES
            LABELS "${abicalls_labels}")
    endforeach()
    add_test(NAME cross_mips_n64
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-n64
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64.cmake")
    set_tests_properties(cross_mips_n64 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.codegen")
    add_test(NAME cross_mips_n64_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-n64-runtime
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
    set_tests_properties(cross_mips_n64_runtime PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen")
    add_test(NAME cross_mips_n64_runtime_large
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-n64-runtime-large
                -DEXTRA_FLAGS=-mcmodel=large
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
    set_tests_properties(cross_mips_n64_runtime_large PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen")
    add_test(NAME cross_mips_x64
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCALLEE=${CMAKE_SOURCE_DIR}/test/target/mips/x64_callee.x
                -DCALLER=${CMAKE_SOURCE_DIR}/test/target/mips/x64_caller.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-x64
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/x64.cmake")
    set_tests_properties(cross_mips_x64 PROPERTIES
        LABELS "check.compile;diagnostics;arch.mips;backend.native;feature.abi;profile.x64")
    # Separately compiled caller and callee; the callee spells -mabi=x.
    foreach(x64_endian big little)
        foreach(x64_level O0 O2)
            set(x64_test cross_mips_x64_runtime_${x64_endian}_${x64_level})
            add_test(NAME ${x64_test}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/x64_caller.x
                    -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/x64_callee.x
                    -DCALLEE_FLAGS=-mabi=x -DMIPS64_ABI=default
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/x64_runtime.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DN64_ENDIAN=${x64_endian} -DEXTRA_FLAGS=-${x64_level}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-x64-runtime-${x64_endian}-${x64_level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${x64_test} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.abi;profile.x64;endian.${x64_endian};opt.${x64_level}")
        endforeach()
    endforeach()
    # A call under a 32-bit-register ABI preserves only the low halves of the
    # VR4300 s registers.
    add_test(NAME cross_mips_preserved_width_assembly COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/preserved_width.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-preserved-width
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/preserved_width.cmake")
    set_tests_properties(cross_mips_preserved_width_assembly PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.abi;feature.register_allocation;opt.O2")
    foreach(abi o32 x32)
        foreach(endian big little)
            string(SUBSTRING ${endian} 0 1 endian_letter)
            foreach(level O0 O2)
                set(name cross_mips_preserved_width_${abi}_${endian_letter}_${level})
                add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/preserved_width.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/preserved_width_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-preserved-width-${abi}-${endian_letter}-${level}
                    -DMIPS_CPUS=vr4300 -DMIPS_ENDIANS=${endian_letter}
                    -DMIPS_SINGLE_CASE=ON -DEXPECTED=P "-DCC_FLAGS=-mabi=${abi};-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                set_tests_properties(${name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.register_allocation;profile.${abi};isa.vr4300;endian.${endian};opt.${level}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()
    # Variadic calls and definitions: o32 and x32 on MIPS III and MIPS I CPUs,
    # x64 with 32-bit addresses on MIPS III, o32 interoperation with a
    # Clang-compiled C reference, and x64 with 64-bit addresses on MIPS64.
    add_test(NAME cross_mips_variadic_assembly COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_assembly.x
        -DMODEL=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_count.xm
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-assembly
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/variadic_assembly.cmake")
    set_tests_properties(cross_mips_variadic_assembly PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.variadic;feature.abi;opt.O2")
    foreach(cpu vr4300 r3000)
        foreach(endian big little)
            string(SUBSTRING ${endian} 0 1 endian_letter)
            foreach(level O0 O2)
                set(variadic_case ${cpu}_${endian_letter}_${level})
                set(variadic_labels "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;isa.${cpu};endian.${endian};opt.${level}")
                set(variadic_runs o32 x32)
                if(cpu STREQUAL "vr4300")
                    list(APPEND variadic_runs x64)
                endif()
                if(level STREQUAL "O2")
                    list(APPEND variadic_runs o32_c)
                endif()
                foreach(run IN LISTS variadic_runs)
                    set(variadic_sources
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_o32.x)
                    set(variadic_flags "-DCC_FLAGS=-${level}")
                    set(variadic_profile o32)
                    if(run STREQUAL "o32_c")
                        list(APPEND variadic_sources
                            -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_o32_c.x
                            -DREFERENCE_C=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_o32.c)
                    elseif(run STREQUAL "x32")
                        set(variadic_sources
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_cross.x)
                        set(variadic_flags "-DCC_FLAGS=-mabi=x32;-DGP=u32;-${level}")
                        set(variadic_profile x32)
                    elseif(run STREQUAL "x64")
                        set(variadic_sources
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_cross.x)
                        set(variadic_flags "-DCC_FLAGS=-mabi=x64;-DGP=u64;-${level}")
                        set(variadic_profile x64)
                    endif()
                    set(name cross_mips_variadic_${run}_${variadic_case})
                    add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        ${variadic_sources}
                        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_start.s
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-${run}-${variadic_case}
                        -DMIPS_CPUS=${cpu} -DMIPS_ENDIANS=${endian_letter}
                        -DMIPS_SINGLE_CASE=ON -DEXPECTED=P "${variadic_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                    set_tests_properties(${name} PROPERTIES
                        LABELS "${variadic_labels};profile.${variadic_profile}"
                        SKIP_REGULAR_EXPRESSION "skipping MIPS")
                endforeach()
            endforeach()
        endforeach()
    endforeach()
    foreach(endian big little)
        foreach(level O0 O2)
            set(name cross_mips_variadic_x64_mips64_${endian}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_cross.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_n64_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DMIPS64_ABI=default -DN64_ENDIAN=${endian}
                "-DEXTRA_FLAGS=-DGP=u64 -${level}"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-x64-mips64-${endian}-${level}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;profile.x64;endian.${endian};opt.${level}")
        endforeach()
    endforeach()
    # Callee home areas of n64 and eabi32 definitions and their va_list
    # interoperation with GCC C: n64 and eabi32 without an FPU forward
    # arg_area, and eabi32 with doubles (R6000) GCC's va_list record.
    add_test(NAME cross_mips_variadic_home COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_home.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-home
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/variadic_home.cmake")
    set_tests_properties(cross_mips_variadic_home PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.variadic;feature.abi;profile.n64;profile.eabi32;endian.big;endian.little;opt.O2")
    foreach(endian big little)
        string(SUBSTRING ${endian} 0 1 endian_letter)
        foreach(level O0 O2)
            set(name cross_mips_variadic_n64_c_${endian}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_n64_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DN64_ENDIAN=${endian}
                "-DEXTRA_FLAGS=-DDOUBLES=1 -${level}"
                -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.c
                "-DGCC_FLAGS=-G0 -DDOUBLES=1"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-n64-c-${endian}-${level}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;feature.interop;profile.n64;isa.mips64;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
            foreach(run eabi32_c eabi32_record)
                if(run STREQUAL "eabi32_c")
                    set(variadic_cpu vr4300)
                    set(variadic_cc "-DCC_FLAGS=-mabi=eabi32;-msoft-float;-${level}")
                    set(variadic_gcc "-DGCC_FLAGS=-mabi=eabi;-mgp32;-msoft-float;-G0")
                    set(variadic_as "-DSTARTUP_AS_FLAGS=-mabi=eabi32;-msoft-float")
                else()
                    set(variadic_cpu r6000)
                    set(variadic_cc "-DCC_FLAGS=-mabi=eabi32;-DRECORD=1;-DDOUBLES=1;-${level}")
                    set(variadic_gcc "-DGCC_FLAGS=-mabi=eabi;-mgp32;-G0;-DDOUBLES=1")
                    set(variadic_as "-DSTARTUP_AS_FLAGS=-mabi=eabi32")
                endif()
                set(name cross_mips_variadic_${run}_${variadic_cpu}_${endian_letter}_${level})
                add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_start.s
                    -DSTARTUP_AS=$<TARGET_FILE:cross_as> "${variadic_as}"
                    -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.c
                    "${variadic_gcc}"
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-${run}-${endian_letter}-${level}
                    -DMIPS_CPUS=${variadic_cpu} -DMIPS_ENDIANS=${endian_letter}
                    -DMIPS_SINGLE_CASE=ON -DEXPECTED=P "${variadic_cc}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                set_tests_properties(${name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;feature.interop;profile.eabi32;isa.${variadic_cpu};endian.${endian};opt.${level}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()
    # Forwarding through the va_list state to GCC C under o32 and n64.
    foreach(level O0 O2)
        set(name cross_mips_variadic_va_list_o32_${level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/va_list.x
            -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/va_list.c
            -DGCC_FLAGS=-G0
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_start.s
            -DCC_FLAGS=-${level} -DEXPECTED=P
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-va-list-o32-${level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;feature.interop;profile.o32;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(endian big little)
            set(name cross_mips_variadic_va_list_n64_${endian}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/va_list.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_n64_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DN64_ENDIAN=${endian} -DEXTRA_FLAGS=-${level}
                -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/va_list.c
                -DGCC_FLAGS=-G0
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-variadic-va-list-n64-${endian}-${level}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;feature.interop;profile.n64;isa.mips64;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
        endforeach()
        # A bare-metal startup that walks the initializer tables.
        set(name cross_mips_init_tables_${level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/init_tables.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/init_tables_start.s
            -DMIPS_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/init_tables.ld
            -DCC_FLAGS=-${level} -DEXPECTED=P
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-init-tables-${level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.init_tables;profile.o32;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_model_mips_abi_properties
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/mips_abi_properties.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-mips-abi-properties
                -P "${CMAKE_SOURCE_DIR}/test/model/mips_abi_properties.cmake")
    set_tests_properties(cross_model_mips_abi_properties PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.abi;feature.model")
    foreach(vla_mode baseline unoptimized compact)
        if(vla_mode STREQUAL "unoptimized")
            set(vla_flags "-O0;-fno-private-abi;-fno-omit-frame-pointer")
        elseif(vla_mode STREQUAL "compact")
            set(vla_flags "-Oz;-fomit-frame-pointer")
        else()
            set(vla_flags "-O3;-fomit-frame-pointer")
        endif()
        add_test(NAME cross_mips_vla_${vla_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vla.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/vla_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vla-${vla_mode}
                -DEXPECTED=P "-DCC_FLAGS=${vla_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_vla_${vla_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.array")
        string(REPLACE ";" " " vla_n64_flags "${vla_flags}")
        foreach(vla_endian big little)
            add_test(NAME cross_mips_n64_vla_${vla_mode}_${vla_endian}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/vla.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/vla_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-vla-n64-${vla_mode}-${vla_endian}
                    -DN64_ENDIAN=${vla_endian} "-DEXTRA_FLAGS=${vla_n64_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_mips_n64_vla_${vla_mode}_${vla_endian} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.array;endian.${vla_endian}")
        endforeach()
    endforeach()
    # Floating values in FPRs on every hard-float CPU, and floating
    # comparisons that branch into phis.
    add_test(NAME cross_mips_fpr_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/fpr_allocation.x
                -DSINGLE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/fpr_allocation_single.x
                -DBRANCH_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/fp_branch.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-fpr-allocation
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/fpr_allocation.cmake")
    set_tests_properties(cross_mips_fpr_allocation PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.register_allocation;feature.floating;isa.r3000;isa.mips2;isa.mips32;isa.mips32r2;isa.vr4300;isa.allegrex;endian.big;endian.little;opt.O2")
    # Each entry: name|-march CPUs, comma-separated|QEMU CPU|level. The
    # harness runs both byte orders.
    foreach(fpr_case
            "r3000_O2|r3000|R4000|O2"
            "r3000_O3|r3000|R4000|O3"
            "mips2_O2|mips2|R4000|O2"
            "vr4300_O2|vr4300|R4000|O2"
            "mips32_O2|mips32,mips32r2|MIPS64R2-generic|O2")
        string(REPLACE "|" ";" fpr_fields "${fpr_case}")
        list(GET fpr_fields 0 fpr_name)
        list(GET fpr_fields 1 fpr_cpus)
        list(GET fpr_fields 2 fpr_qemu)
        list(GET fpr_fields 3 fpr_level)
        string(REPLACE "," ";" fpr_cpus "${fpr_cpus}")
        set(fpr_labels "check.runtime;arch.mips;backend.native;feature.floating;endian.big;endian.little;opt.${fpr_level}")
        foreach(fpr_cpu IN LISTS fpr_cpus)
            list(APPEND fpr_labels "isa.${fpr_cpu}")
        endforeach()
        foreach(fpr_test fpr_allocation fp_branch)
            add_test(NAME cross_mips_${fpr_test}_runtime_${fpr_name}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/${fpr_test}.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-${fpr_test}-runtime-${fpr_name}
                    -DEXPECTED=P "-DCC_FLAGS=-${fpr_level};-fno-eval-calls"
                    "-DMIPS_CPUS=${fpr_cpus}" -DQEMU_CPU=${fpr_qemu}
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_mips_${fpr_test}_runtime_${fpr_name} PROPERTIES
                LABELS "${fpr_labels};feature.register_allocation"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_mips_fp_branch_runtime_unfused
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/fp_branch.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-fp_branch-runtime-unfused
            -DEXPECTED=P "-DCC_FLAGS=-O2;-fno-eval-calls;-fno-compare-branch-fusion"
            "-DMIPS_CPUS=vr4300;r3000"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_mips_fp_branch_runtime_unfused PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.floating;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    # 32-bit rotates on CPUs with 64-bit GPRs; integer code, so FP32 objects
    # link with the startup whatever FPU the CPU has.
    foreach(rotate_level O0 O2)
        add_test(NAME cross_mips_rotate_wide_gpr_runtime_${rotate_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/rotate_wide_gpr.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-rotate-wide-gpr-${rotate_level}
                -DEXPECTED=P "-DCC_FLAGS=-${rotate_level};-fno-eval-calls;-mfp32"
                "-DMIPS_CPUS=mips64r2;octeon;vr5400;sr71000;gs464;r10000"
                -DQEMU_CPU=MIPS64R2-generic
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_rotate_wide_gpr_runtime_${rotate_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.intrinsics;isa.mips64r2;isa.octeon;isa.vr5400;isa.sr71000;isa.gs464;isa.r10000;endian.big;endian.little;opt.${rotate_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # The errata workarounds that CPU entries and -mfix-NAME select.
    add_test(NAME cross_mips_errata
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/errata.x
                -DLOOPS_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/short_loops.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-errata/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/errata.cmake")
    set_tests_properties(cross_mips_errata PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.errata;feature.atomics;feature.delay_slots;isa.r4000;isa.r4400;isa.r10000;isa.gs464;isa.r5900;isa.vr4300;endian.big;opt.O2;opt.O3;opt.Os;opt.Oz")
    # The Release 2 selections (SEB, SEH, WSBH, DSBH, DSHD, EXT, INS, DEXT,
    # DINS, DROTR, MUL): which CPUs select them, and their results under
    # o32 on MIPS32 and MIPS64 Release 2 CPUs and under n64.
    add_test(NAME cross_mips_release2
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release2.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release2/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/release2.cmake")
    set_tests_properties(cross_mips_release2 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.codegen;isa.mips32r2;isa.mips64r2;isa.allegrex;isa.vr4300;isa.mips4;isa.mips32;endian.big;endian.little;opt.O0;opt.O2")
    foreach(release2_level O0 O2)
        add_test(NAME cross_mips_release2_runtime_${release2_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release2.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release2-${release2_level}
                -DEXPECTED=P "-DCC_FLAGS=-${release2_level};-fno-eval-calls;-mfp32"
                "-DMIPS_CPUS=mips32r2;24kc;mips64r2;octeon3;gs464e"
                -DQEMU_CPU=MIPS64R2-generic
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_release2_runtime_${release2_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.codegen;isa.mips32r2;isa.24kc;isa.mips64r2;isa.octeon3;isa.gs464e;endian.big;endian.little;opt.${release2_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(release2_endian big little)
            add_test(NAME cross_mips_release2_n64_${release2_endian}_${release2_level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release2.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release2-n64-${release2_endian}-${release2_level}
                    -DN64_ENDIAN=${release2_endian}
                    "-DEXTRA_FLAGS=-${release2_level} -fno-eval-calls -march=mips64r2"
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_mips_release2_n64_${release2_endian}_${release2_level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.codegen;isa.mips64r2;profile.n64;endian.${release2_endian};opt.${release2_level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # Release 6: what its CPUs select, the objects' ISA, NaN encoding, and
    # FPU register model, the options it rejects, and GNU as's encodings.
    add_test(NAME cross_mips_release6
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6/case
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/release6.cmake")
    set_tests_properties(cross_mips_release6 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.codegen;feature.floating;isa.mips32r6;isa.mips64r6;isa.i6400;isa.i6500;isa.p6600;isa.mips32r2;isa.mips64r2;isa.vr4300;isa.mips4;endian.big;endian.little;opt.O0;opt.O2")
    # The Release 6 results under QEMU's Release 6 CPUs, whose startups
    # MIPS_RELEASE6 assembles at the Release 6 level: o32 on MIPS32R6 and
    # MIPS64R6 CPUs in both byte orders, also as FPXX code, and n64, and
    # floating calls in both directions with GCC objects. Each o32 entry:
    # name|CPUs|QEMU CPU|options.
    foreach(release6_level O0 O2)
        foreach(release6_case "mips32r6|mips32r6|mips32r6-generic|-fno-eval-calls"
                              "mips32r6_fpxx|mips32r6|mips32r6-generic|-fno-eval-calls,-mfpxx"
                              "mips64r6|mips64r6,i6400,i6500,p6600|I6400|-fno-eval-calls")
            string(REPLACE "|" ";" release6_fields "${release6_case}")
            list(GET release6_fields 0 release6_name)
            list(GET release6_fields 1 release6_cpus)
            list(GET release6_fields 2 release6_qemu)
            list(GET release6_fields 3 release6_options)
            string(REPLACE "," ";" release6_cpus "${release6_cpus}")
            string(REPLACE "," ";" release6_options "${release6_options}")
            set(release6_labels "check.runtime;arch.mips;backend.native;feature.codegen;feature.floating;feature.atomics;endian.big;endian.little;opt.${release6_level}")
            foreach(release6_cpu IN LISTS release6_cpus)
                list(APPEND release6_labels "isa.${release6_cpu}")
            endforeach()
            add_test(NAME cross_mips_release6_runtime_${release6_name}_${release6_level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-${release6_name}-${release6_level}
                    -DEXPECTED=P "-DCC_FLAGS=-${release6_level};${release6_options}"
                    "-DMIPS_CPUS=${release6_cpus}" -DQEMU_CPU=${release6_qemu}
                    -DMIPS_RELEASE6=ON
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_mips_release6_runtime_${release6_name}_${release6_level}
                PROPERTIES LABELS "${release6_labels}" SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
        add_test(NAME cross_mips_release6_interop_${release6_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6_interop.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6_interop.c
                -DGCC_FLAGS=-G0
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-interop-${release6_level}
                -DEXPECTED=P "-DCC_FLAGS=-${release6_level};-fno-eval-calls"
                -DMIPS_CPUS=mips32r6 -DQEMU_CPU=mips32r6-generic -DMIPS_RELEASE6=ON
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_release6_interop_${release6_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.floating;feature.interop;profile.o32;isa.mips32r6;endian.big;endian.little;opt.${release6_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(release6_endian big little)
            add_test(NAME cross_mips_release6_n64_${release6_endian}_${release6_level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-n64-${release6_endian}-${release6_level}
                    -DN64_ENDIAN=${release6_endian}
                    "-DEXTRA_FLAGS=-${release6_level} -fno-eval-calls -march=mips64r6"
                    -DN64_QEMU_CPU=I6400 -DMIPS_RELEASE6=ON
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_mips_release6_n64_${release6_endian}_${release6_level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.codegen;feature.floating;feature.atomics;profile.n64;isa.mips64r6;endian.${release6_endian};opt.${release6_level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
            add_test(NAME cross_mips_release6_n64_interop_${release6_endian}_${release6_level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6_interop.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-n64-interop-${release6_endian}-${release6_level}
                    -DN64_ENDIAN=${release6_endian}
                    "-DEXTRA_FLAGS=-${release6_level} -fno-eval-calls -march=i6500"
                    -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/release6_interop.c
                    "-DGCC_FLAGS=-G0 -march=i6500"
                    -DN64_QEMU_CPU=I6500 -DMIPS_RELEASE6=ON
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_mips_release6_n64_interop_${release6_endian}_${release6_level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.floating;feature.interop;profile.n64;isa.i6500;endian.${release6_endian};opt.${release6_level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
        endforeach()
    endforeach()
    # Records by value and variadic calls across the n64 C ABI with GCC
    # objects compiled for Release 6.
    foreach(release6_endian big little)
        add_test(NAME cross_mips_release6_n64_records_c_${release6_endian}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_records.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/n64_records_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DN64_ENDIAN=${release6_endian}
                "-DEXTRA_FLAGS=-O2 -march=mips64r6"
                -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_records.c
                "-DGCC_FLAGS=-G0 -march=mips64r6"
                -DN64_QEMU_CPU=I6400 -DMIPS_RELEASE6=ON
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-n64-records-c-${release6_endian}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
        set_tests_properties(cross_mips_release6_n64_records_c_${release6_endian} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.interop;feature.records;profile.n64;isa.mips64r6;endian.${release6_endian};opt.O2"
            SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
        add_test(NAME cross_mips_release6_variadic_n64_c_${release6_endian}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_n64_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DN64_ENDIAN=${release6_endian}
                "-DEXTRA_FLAGS=-DDOUBLES=1 -O2 -march=mips64r6"
                -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/variadic_c.c
                "-DGCC_FLAGS=-G0 -DDOUBLES=1 -march=mips64r6"
                -DN64_QEMU_CPU=I6400 -DMIPS_RELEASE6=ON
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-release6-variadic-n64-c-${release6_endian}
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
        set_tests_properties(cross_mips_release6_variadic_n64_c_${release6_endian} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.variadic;feature.abi;feature.interop;profile.n64;isa.mips64r6;endian.${release6_endian};opt.O2"
            SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
    endforeach()
    # The differential kernels on Release 6 against the x86-64 reference.
    # Each entry: name|endian|address model|QEMU CPU|optimization|Release 6
    # level|flags; n64 entries only run the sets with n64 startups.
    foreach(release6_set int float private quality boundary wide)
        foreach(release6_case
                "mips32r6|BE|32|mips32r6-generic|-O2|mips32r6|-target mips-unknown-elf -mabi=o32 -march=mips32r6"
                "mips32r6_le_o0|LE|32|mips32r6-generic|-O0|mips32r6|-target mipsel-unknown-elf -mabi=o32 -march=mips32r6"
                "i6400_o32|BE|32|I6400|-O2|mips64r6|-target mips-unknown-elf -mabi=o32 -march=i6400"
                "n64|BE|64|I6400|-O2|mips64r6|-target mips64-unknown-elf -mabi=n64 -march=mips64r6"
                "n64_le_o0|LE|64|I6500|-O0|mips64r6|-target mips64el-unknown-elf -mabi=n64 -march=i6500")
            string(REPLACE "|" ";" release6_fields "${release6_case}")
            list(GET release6_fields 0 release6_name)
            list(GET release6_fields 1 release6_endian)
            list(GET release6_fields 2 release6_address)
            list(GET release6_fields 3 release6_qemu)
            list(GET release6_fields 4 release6_level)
            list(GET release6_fields 5 release6_isa)
            list(GET release6_fields 6 release6_flags)
            if(release6_address STREQUAL "64")
                if(NOT release6_set MATCHES "^(quality|boundary|wide)$")
                    continue()
                endif()
                set(release6_startup start_${release6_set}_n64.s)
                set(release6_linker bare-n64.ld)
            else()
                set(release6_startup start_${release6_set}.s)
                set(release6_linker bare.ld)
            endif()
            if(release6_endian STREQUAL "LE")
                set(release6_endian_label little)
            else()
                set(release6_endian_label big)
            endif()
            string(SUBSTRING "${release6_level}" 1 -1 release6_level_label)
            add_test(NAME cross_mips_differential_${release6_set}_r6_${release6_name}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/${release6_set}_kernels.x.in
                        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/${release6_startup}
                        -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_${release6_set}.cpp
                        -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/${release6_linker}
                        -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                        -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                        "-DTARGET_FLAGS=${release6_flags}"
                        -DENDIAN=${release6_endian}
                        -DADDRESS_MODEL=${release6_address}
                        -DQEMU_CPU=${release6_qemu}
                        -DOPT_LEVEL=${release6_level}
                        -DRELEASE6=${release6_isa}
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-${release6_set}-r6-${release6_name}
                        -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
            set_tests_properties(cross_mips_differential_${release6_set}_r6_${release6_name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.codegen;isa.${release6_isa};endian.${release6_endian_label};opt.${release6_level_label}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # The floating kernels with FPR allocation before MIPS III, against the
    # x86-64 reference. MIPS I lacks the float-to-integer conversion they use.
    # Each entry: name|endian|QEMU CPU|flags.
    foreach(float_case
            "mips2|BE|R4000|-target mips-unknown-elf -mabi=o32 -march=mips2"
            "mips2_le|LE|R4000|-target mipsel-unknown-elf -mabi=o32 -march=mips2"
            "mips32|BE|24Kf|-target mips-unknown-elf -mabi=o32 -march=mips32"
            "mips32_le|LE|24Kf|-target mipsel-unknown-elf -mabi=o32 -march=mips32")
        string(REPLACE "|" ";" float_fields "${float_case}")
        list(GET float_fields 0 float_name)
        list(GET float_fields 1 float_endian)
        list(GET float_fields 2 float_cpu)
        list(GET float_fields 3 float_flags)
        string(REGEX REPLACE "_le$" "" float_isa "${float_name}")
        if(float_endian STREQUAL "LE")
            set(float_endian_label little)
        else()
            set(float_endian_label big)
        endif()
        add_test(NAME cross_mips_differential_float_${float_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/float_kernels.x.in
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_float.s
                    -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_float.cpp
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                    -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                    -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                    "-DTARGET_FLAGS=${float_flags}"
                    -DENDIAN=${float_endian}
                    -DQEMU_CPU=${float_cpu}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-float-${float_name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
        set_tests_properties(cross_mips_differential_float_${float_name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.floating;feature.register_allocation;isa.${float_isa};endian.${float_endian_label};opt.O2")
    endforeach()
    add_test(NAME cross_mips_differential_int_vr4300
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/int_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_int.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_int.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=vr4300-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-int-vr4300
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_int_vr4300 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    add_test(NAME cross_mips_differential_int_vr4300_ld
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DLD=$<TARGET_FILE:cross_ld>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/int_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_int.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_int.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=vr4300-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-int-vr4300-ld
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_int_vr4300_ld PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization;feature.linker")
    add_test(NAME cross_mips_differential_int_r3000
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/int_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_int.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_int.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=r3000-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-int-r3000
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_int_r3000 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    add_test(NAME cross_mips_differential_int_vr4300_le
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/int_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_int.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_int.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                "-DTARGET_FLAGS=-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
                -DENDIAN=LE
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-int-vr4300-le
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_int_vr4300_le PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    add_test(NAME cross_mips_differential_int_vr4300_os
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/int_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_int.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_int.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=vr4300-o32
                -DOPT_LEVEL=-Os
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-int-vr4300-os
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_int_vr4300_os PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization;opt.Os")
    add_test(NAME cross_mips_differential_float_vr4300
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/float_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_float.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_float.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=vr4300-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-float-vr4300
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_float_vr4300 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    # k_p_float lives in the float set (see float_kernels.x.in), so the
    # private set is integer-only and runs on both CPU profiles below.
    add_test(NAME cross_mips_differential_private_vr4300
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/private_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_private.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_private.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=vr4300-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-private-vr4300
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_private_vr4300 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    add_test(NAME cross_mips_differential_private_r3000
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/private_kernels.x.in
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/start_private.s
                -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_private.cpp
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
                -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                -DTARGET_FLAGS=-mprofile=r3000-o32
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-private-r3000
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
    set_tests_properties(cross_mips_differential_private_r3000 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.optimization")
    # Global addressing, immediate comparisons, filled delay slots, loop
    # constants, narrow values, and shared addresses on each CPU class and
    # byte order, under the N64 console profile's trapping floating
    # environment and the VR4300's Cross ABI profile, on Release 2 CPUs,
    # and with the enabling controls switched off for comparison. Each
    # entry:
    # name|ISA|endian|address model|QEMU CPU|optimization|flags.
    foreach(quality_case
            "vr4300|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-o32"
            "vr4300_le|vr4300|LE|32|R4000|-O2|-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
            "vr4300_o3|vr4300|BE|32|R4000|-O3|-mprofile=vr4300-o32"
            "vr4300_o3_le|vr4300|LE|32|R4000|-O3|-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
            "vr4300_os|vr4300|BE|32|R4000|-Os|-mprofile=vr4300-o32"
            "vr4300_oz|vr4300|BE|32|R4000|-Oz|-mprofile=vr4300-o32"
            "vr4300_console|vr4300|BE|32|R4000|-O2|-mprofile=n64"
            "vr4300_console_o3|vr4300|BE|32|R4000|-O3|-mprofile=n64"
            "vr4300_controls_off|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-o32 -fno-schedule-insns2 -fno-combine-addresses -fno-machine-combine -fno-move-loop-invariants -fno-tree-reassoc"
            "r3000|r3000|BE|32|R4000|-O2|-mprofile=r3000-o32"
            "r3000_le|r3000|LE|32|R4000|-O2|-target mipsel-unknown-elf -mabi=o32 -march=r3000"
            "mips32|mips32|BE|32|24Kf|-O2|-target mips-unknown-elf -mabi=o32 -march=mips32"
            "mips32_le|mips32|LE|32|24Kf|-O2|-target mipsel-unknown-elf -mabi=o32 -march=mips32"
            "n64|mips64|BE|64|MIPS64R2-generic|-O2|-mprofile=mips64-n64 -mabi=n64"
            "n64_le|mips64|LE|64|MIPS64R2-generic|-O2|-mprofile=mips64el-n64 -mabi=n64"
            "vr4300_x64|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-x64"
            "vr4300_x64_oz|vr4300|BE|32|R4000|-Oz|-mprofile=vr4300-x64"
            "mips32r2|mips32r2|BE|32|24Kf|-O2|-target mips-unknown-elf -mabi=o32 -march=mips32r2"
            "mips32r2_oz_le|mips32r2|LE|32|24Kf|-Oz|-target mipsel-unknown-elf -mabi=o32 -march=mips32r2"
            "mips64r2_o3|mips64r2|BE|64|MIPS64R2-generic|-O3|-mprofile=mips64-n64 -mabi=n64 -march=mips64r2")
        string(REPLACE "|" ";" quality_fields "${quality_case}")
        list(GET quality_fields 0 quality_name)
        list(GET quality_fields 1 quality_isa)
        list(GET quality_fields 2 quality_endian)
        list(GET quality_fields 3 quality_address)
        list(GET quality_fields 4 quality_cpu)
        list(GET quality_fields 5 quality_level)
        list(GET quality_fields 6 quality_flags)
        string(SUBSTRING "${quality_level}" 1 -1 quality_level_label)
        if(quality_address STREQUAL "64")
            set(quality_startup start_quality_n64.s)
            set(quality_linker bare-n64.ld)
        else()
            set(quality_startup start_quality.s)
            set(quality_linker bare.ld)
        endif()
        if(quality_endian STREQUAL "LE")
            set(quality_endian_label little)
        else()
            set(quality_endian_label big)
        endif()
        add_test(NAME cross_mips_differential_quality_${quality_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/quality_kernels.x.in
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/${quality_startup}
                    -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_quality.cpp
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/${quality_linker}
                    -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                    -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                    "-DTARGET_FLAGS=${quality_flags}"
                    -DENDIAN=${quality_endian}
                    -DADDRESS_MODEL=${quality_address}
                    -DQEMU_CPU=${quality_cpu}
                    -DOPT_LEVEL=${quality_level}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-quality-${quality_name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
        set_tests_properties(cross_mips_differential_quality_${quality_name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.optimization;isa.${quality_isa};endian.${quality_endian_label};opt.${quality_level_label}")
    endforeach()
    # u64 parameters and results at o32, x32, and n64 boundaries in both
    # byte orders, with and without the boundary narrowing. Each entry:
    # name|ISA|endian|address model|QEMU CPU|optimization|flags.
    foreach(boundary_case
            "o32_o0|vr4300|BE|32|R4000|-O0|-mprofile=vr4300-o32"
            "o32_o2|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-o32"
            "o32_oz|vr4300|BE|32|R4000|-Oz|-mprofile=vr4300-o32"
            "o32_le_o0|vr4300|LE|32|R4000|-O0|-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
            "o32_le_o2|vr4300|LE|32|R4000|-O2|-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
            "o32_le_oz|vr4300|LE|32|R4000|-Oz|-target mipsel-unknown-elf -mabi=o32 -march=vr4300"
            "o32_whole_o2|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-o32 -fno-machine-combine -fno-cprop-registers"
            "console_o3|vr4300|BE|32|R4000|-O3|-mprofile=n64"
            "x32_o0|vr4300|BE|32|R4000|-O0|-target mips-unknown-elf -march=vr4300"
            "x32_o2|vr4300|BE|32|R4000|-O2|-target mips-unknown-elf -march=vr4300"
            "x32_le_o0|vr4300|LE|32|R4000|-O0|-target mipsel-unknown-elf -march=vr4300"
            "x32_le_o2|vr4300|LE|32|R4000|-O2|-target mipsel-unknown-elf -march=vr4300"
            "x32_oz|vr4300|BE|32|R4000|-Oz|-target mips-unknown-elf -march=vr4300"
            "r3000_o2|r3000|BE|32|R4000|-O2|-mprofile=r3000-o32"
            "n64_o0|mips64|BE|64|MIPS64R2-generic|-O0|-mprofile=mips64-n64 -mabi=n64"
            "n64_o2|mips64|BE|64|MIPS64R2-generic|-O2|-mprofile=mips64-n64 -mabi=n64"
            "n64_le_o0|mips64|LE|64|MIPS64R2-generic|-O0|-mprofile=mips64el-n64 -mabi=n64"
            "n64_le_o2|mips64|LE|64|MIPS64R2-generic|-O2|-mprofile=mips64el-n64 -mabi=n64")
        string(REPLACE "|" ";" boundary_fields "${boundary_case}")
        list(GET boundary_fields 0 boundary_name)
        list(GET boundary_fields 1 boundary_isa)
        list(GET boundary_fields 2 boundary_endian)
        list(GET boundary_fields 3 boundary_address)
        list(GET boundary_fields 4 boundary_cpu)
        list(GET boundary_fields 5 boundary_level)
        list(GET boundary_fields 6 boundary_flags)
        string(SUBSTRING "${boundary_level}" 1 -1 boundary_level_label)
        if(boundary_address STREQUAL "64")
            set(boundary_startup start_boundary_n64.s)
            set(boundary_linker bare-n64.ld)
        else()
            set(boundary_startup start_boundary.s)
            set(boundary_linker bare.ld)
        endif()
        if(boundary_endian STREQUAL "LE")
            set(boundary_endian_label little)
        else()
            set(boundary_endian_label big)
        endif()
        add_test(NAME cross_mips_differential_boundary_${boundary_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/boundary_kernels.x.in
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/${boundary_startup}
                    -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_boundary.cpp
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/${boundary_linker}
                    -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                    -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                    "-DTARGET_FLAGS=${boundary_flags}"
                    -DENDIAN=${boundary_endian}
                    -DADDRESS_MODEL=${boundary_address}
                    -DQEMU_CPU=${boundary_cpu}
                    -DOPT_LEVEL=${boundary_level}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-boundary-${boundary_name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
        set_tests_properties(cross_mips_differential_boundary_${boundary_name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.optimization;isa.${boundary_isa};endian.${boundary_endian_label};opt.${boundary_level_label}")
    endforeach()
    # 128-bit integers against the x86-64 reference: o32 kernels call helpers
    # in the private ABI or, with -fno-private-abi, in the compilation's x32,
    # eabi32, x64, or n64. Each entry: name|ISA|endian|address model|QEMU
    # CPU|optimization|flags|ABI of the startup (- for llvm-mc's o32).
    foreach(wide_case
            "vr4300_o0|vr4300|BE|32|R4000|-O0|-mprofile=vr4300-o32|-"
            "vr4300_o2|vr4300|BE|32|R4000|-O2|-mprofile=vr4300-o32|-"
            "vr4300_le_o2|vr4300|LE|32|R4000|-O2|-target mipsel-unknown-elf -mabi=o32 -march=vr4300|-"
            "vr4300_os|vr4300|BE|32|R4000|-Os|-mprofile=vr4300-o32|-"
            "vr4300_o3|vr4300|BE|32|R4000|-O3|-mprofile=vr4300-o32|-"
            "r3000_o2|r3000|BE|32|R4000|-O2|-mprofile=r3000-o32|-"
            "r3000_le_o0|r3000|LE|32|R4000|-O0|-target mipsel-unknown-elf -mabi=o32 -march=r3000|-"
            "mips2_o2|mips2|BE|32|R4000|-O2|-target mips-unknown-elf -mabi=o32 -march=mips2|-"
            "mips32_o2|mips32|BE|32|24Kf|-O2|-target mips-unknown-elf -mabi=o32 -march=mips32|-"
            "mips32_le_o2|mips32|LE|32|24Kf|-O2|-target mipsel-unknown-elf -mabi=o32 -march=mips32|-"
            "x32_o2|vr4300|BE|32|R4000|-O2|-target mips-unknown-elf -march=vr4300 -fno-private-abi|-"
            "x32_r3000_le_o2|r3000|LE|32|R4000|-O2|-target mipsel-unknown-elf -march=r3000 -fno-private-abi|-"
            "eabi32_o2|vr4300|BE|32|R4000|-O2|-target mips-unknown-elf -mabi=eabi32 -march=vr4300 -fno-private-abi|eabi32"
            "eabi32_mips2_le_o0|mips2|LE|32|R4000|-O0|-target mipsel-unknown-elf -mabi=eabi32 -march=mips2 -fno-private-abi|eabi32"
            "x64_o2|vr4300|BE|32|R4000|-O2|-target mips-unknown-elf -mabi=x64 -march=vr4300 -fno-private-abi|-"
            "n64_o0|mips64|BE|64|MIPS64R2-generic|-O0|-mprofile=mips64-n64 -mabi=n64 -fno-private-abi|-"
            "n64_o2|mips64|BE|64|MIPS64R2-generic|-O2|-mprofile=mips64-n64 -mabi=n64 -fno-private-abi|-"
            "n64_le_o2|mips64|LE|64|MIPS64R2-generic|-O2|-mprofile=mips64el-n64 -mabi=n64 -fno-private-abi|-"
            "x64_64_o2|mips64|BE|64|MIPS64R2-generic|-O2|-mprofile=mips64-n64 -fno-private-abi|-"
            "x64_64_le_o3|mips64|LE|64|MIPS64R2-generic|-O3|-mprofile=mips64el-n64|-")
        string(REPLACE "|" ";" wide_fields "${wide_case}")
        list(GET wide_fields 0 wide_name)
        list(GET wide_fields 1 wide_isa)
        list(GET wide_fields 2 wide_endian)
        list(GET wide_fields 3 wide_address)
        list(GET wide_fields 4 wide_cpu)
        list(GET wide_fields 5 wide_level)
        list(GET wide_fields 6 wide_flags)
        list(GET wide_fields 7 wide_startup_abi)
        string(SUBSTRING "${wide_level}" 1 -1 wide_level_label)
        if(wide_address STREQUAL "64")
            set(wide_startup start_wide_n64.s)
            set(wide_linker bare-n64.ld)
        else()
            set(wide_startup start_wide.s)
            set(wide_linker bare.ld)
        endif()
        set(wide_assembler "")
        if(NOT wide_startup_abi STREQUAL "-")
            set(wide_assembler -DSTARTUP_AS=$<TARGET_FILE:cross_as>
                "-DSTARTUP_AS_FLAGS=-mabi=${wide_startup_abi}")
        endif()
        if(wide_endian STREQUAL "LE")
            set(wide_endian_label little)
        else()
            set(wide_endian_label big)
        endif()
        add_test(NAME cross_mips_differential_wide_${wide_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DTEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/differential/wide_kernels.x.in
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/differential/${wide_startup}
                    -DHOST_MAIN=${CMAKE_SOURCE_DIR}/test/target/mips/differential/main_wide.cpp
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/${wide_linker}
                    -DWRAPPER_TEMPLATE=${CMAKE_SOURCE_DIR}/test/target/mips/runtime64.s.in
                    -DWRAPPER_LINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare64.ld
                    "-DTARGET_FLAGS=${wide_flags}"
                    -DENDIAN=${wide_endian}
                    -DADDRESS_MODEL=${wide_address}
                    -DQEMU_CPU=${wide_cpu}
                    -DOPT_LEVEL=${wide_level}
                    ${wide_assembler}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-differential-wide-${wide_name}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/differential/differential.cmake")
        set_tests_properties(cross_mips_differential_wide_${wide_name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.integer128;feature.abi;isa.${wide_isa};endian.${wide_endian_label};opt.${wide_level_label}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # 128-bit integers and records by value across the n64 C ABI: Cross and
    # GNU GCC call each other in both directions.
    foreach(interop int128 records)
        foreach(endian big little)
            foreach(level O0 O2)
                set(name cross_mips_n64_${interop}_c_${endian}_${level})
                add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_${interop}.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/n64_${interop}_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DN64_ENDIAN=${endian}
                    "-DEXTRA_FLAGS=-${level}"
                    -DGCC_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/n64_${interop}.c
                    "-DGCC_FLAGS=-G0"
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-n64-${interop}-c-${endian}-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
                if(interop STREQUAL "int128")
                    set(interop_label feature.integer128)
                else()
                    set(interop_label feature.records)
                endif()
                set_tests_properties(${name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.interop;${interop_label};profile.n64;isa.mips64;endian.${endian};opt.${level}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS n64")
            endforeach()
        endforeach()
    endforeach()
    # 128-bit placement and inline sequences in the assembly, and the
    # 128-bit atomics, naked interfaces, and patch values MIPS diagnoses.
    add_test(NAME cross_mips_int128 COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/int128_errors.x
        -DCODEGEN_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/int128_codegen.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-int128
        -P "${CMAKE_SOURCE_DIR}/test/target/mips/int128.cmake")
    set_tests_properties(cross_mips_int128 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.integer128;feature.abi;diagnostics;isa.vr4300;isa.mips64;profile.o32;profile.eabi32;profile.x32;profile.n64;endian.big;endian.little")
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
    set_tests_properties(cross_model_system PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.model;profile.custom")
    add_test(NAME cross_model_validation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-validation
                -P "${CMAKE_SOURCE_DIR}/test/model/model_validation.cmake")
    set_tests_properties(cross_model_validation PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver")
    add_test(NAME cross_model_instruction_entries
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DAS=$<TARGET_FILE:cross_as>
                -DFIXTURES=${CMAKE_SOURCE_DIR}/test/model/instruction_entries
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-instruction-entries
                -P "${CMAKE_SOURCE_DIR}/test/model/instruction_entries.cmake")
    set_tests_properties(cross_model_instruction_entries PROPERTIES
        LABELS "check.compile;diagnostics;arch.mips;backend.native;feature.model;feature.assembler;feature.raw")
    add_test(NAME cross_model_debug_entries
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-debug-entries
                -P "${CMAKE_SOURCE_DIR}/test/model/debug_entries.cmake")
    set_tests_properties(cross_model_debug_entries PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.model;feature.driver;feature.debug")
    add_test(NAME cross_model_cpu_entries
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DROTATE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/allegrex.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-cpu-entries
                -P "${CMAKE_SOURCE_DIR}/test/model/cpu_entries.cmake")
    set_tests_properties(cross_model_cpu_entries PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver;feature.optimization")
    add_test(NAME cross_model_function_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-function-alignment
                -P "${CMAKE_SOURCE_DIR}/test/model/function_alignment.cmake")
    set_tests_properties(cross_model_function_alignment PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.alignment;opt.O2;opt.Os")
    add_test(NAME cross_model_cpu_catalog
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-cpu-catalog
                -P "${CMAKE_SOURCE_DIR}/test/model/cpu_catalog.cmake")
    set_tests_properties(cross_model_cpu_catalog PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver;feature.assembler")
    add_test(NAME cross_model_cpu_catalog_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-cpu-catalog-codegen
                -P "${CMAKE_SOURCE_DIR}/test/model/cpu_catalog_codegen.cmake")
    set_tests_properties(cross_model_cpu_catalog_codegen PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;backend.native;feature.model;feature.codegen;feature.intrinsics;feature.atomics;feature.vector;feature.floating")
    add_test(NAME cross_model_inheritance
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-inheritance
                -P "${CMAKE_SOURCE_DIR}/test/model/model_inheritance.cmake")
    set_tests_properties(cross_model_inheritance PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.optimization")
    # The AArch64 architecture: registers, features and their implications,
    # -march modifiers, the CPU catalog, ABIs, profiles, and the absence of
    # code generation.
    add_test(NAME cross_aarch64_models
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCATALOG=${CMAKE_SOURCE_DIR}/test/target/aarch64/catalog.txt
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-models
                -P "${CMAKE_SOURCE_DIR}/test/target/aarch64/models.cmake")
    set_tests_properties(cross_aarch64_models PROPERTIES
        LABELS "check.compile;diagnostics;arch.aarch64;feature.model;feature.driver;feature.abi")
    add_test(NAME cross_aarch64_codegen_unavailable
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-codegen-unavailable
                -P "${CMAKE_SOURCE_DIR}/test/target/aarch64/codegen_unavailable.cmake")
    set_tests_properties(cross_aarch64_codegen_unavailable PROPERTIES
        LABELS "check.compile;diagnostics;arch.aarch64;feature.driver")
    # The AArch64 assembler: every form of the shipped table, relocations,
    # and directives against llvm-mc (and GNU as, where installed); values
    # that only Cross takes; diagnostics; a bare-metal link with ld.lld and
    # GNU ld, run on QEMU where installed; and user instruction entries.
    set(aarch64_assembler ${CMAKE_SOURCE_DIR}/test/target/aarch64/assembler)
    add_test(NAME cross_aarch64_assembler_table
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${aarch64_assembler}/table.s
                -DGNU_SOURCES=${aarch64_assembler}/table_gnu.s
                "-DOPTIONS=-march=armv8.2-a+fp16+crc+lse+sb+jscvt"
                "-DMC_OPTIONS=-mattr=+v8.2a,+fullfp16,+crc,+lse,+sb,+jsconv"
                "-DGNU_OPTIONS=-march=armv8.3-a+fp16+crc+lse+sb"
                -DGNU_TEXT=1
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/table
                -P "${aarch64_assembler}/differential.cmake")
    set_tests_properties(cross_aarch64_assembler_table PROPERTIES
        LABELS "feature.assembler;feature.model;arch.aarch64;check.compile;endian.little;feature.floating;feature.atomics")
    foreach(case relocations directives)
        add_test(NAME cross_aarch64_assembler_${case}
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${aarch64_assembler}/${case}.s
                    "-DOPTIONS=-march=armv8-a"
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/${case}
                    -P "${aarch64_assembler}/differential.cmake")
    endforeach()
    set_tests_properties(cross_aarch64_assembler_relocations PROPERTIES
        LABELS "feature.assembler;arch.aarch64;check.compile;feature.object;feature.tls;feature.pic")
    set_tests_properties(cross_aarch64_assembler_directives PROPERTIES
        LABELS "feature.assembler;arch.aarch64;check.compile;feature.object;feature.debug")
    add_test(NAME cross_aarch64_assembler_gnu
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DSOURCE=${aarch64_assembler}/relocations_gnu.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/gnu
                -P "${aarch64_assembler}/gnu.cmake")
    set_tests_properties(cross_aarch64_assembler_gnu PROPERTIES
        LABELS "feature.assembler;arch.aarch64;check.compile;feature.object;feature.tls")
    add_test(NAME cross_aarch64_assembler_words
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DSOURCE=${aarch64_assembler}/words.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/words
                -P "${aarch64_assembler}/words.cmake")
    set_tests_properties(cross_aarch64_assembler_words PROPERTIES
        LABELS "feature.assembler;arch.aarch64;check.compile")
    add_test(NAME cross_aarch64_assembler_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/diagnostics
                -P "${aarch64_assembler}/diagnostics.cmake")
    set_tests_properties(cross_aarch64_assembler_diagnostics PROPERTIES
        LABELS "feature.assembler;arch.aarch64;diagnostics")
    add_test(NAME cross_aarch64_assembler_link
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DSOURCE=${aarch64_assembler}/link.s
                -DSCRIPT=${aarch64_assembler}/bare.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-assembler/link
                -P "${aarch64_assembler}/link.cmake")
    set_tests_properties(cross_aarch64_assembler_link PROPERTIES
        LABELS "feature.assembler;feature.linker;arch.aarch64;check.runtime;feature.tls;feature.pic;feature.floating;feature.atomics")
    add_test(NAME cross_aarch64_instruction_entries
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aarch64-instruction-entries
                -P "${CMAKE_SOURCE_DIR}/test/target/aarch64/instruction_entries.cmake")
    set_tests_properties(cross_aarch64_instruction_entries PROPERTIES
        LABELS "feature.model;feature.assembler;arch.aarch64;diagnostics")
    # Loading checks of uniform_members, max_members, exhaust, implications,
    # and call_preserved_views.
    add_test(NAME cross_model_homogeneous_validation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/homogeneous.xm
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-homogeneous-validation
                -P "${CMAKE_SOURCE_DIR}/test/model/homogeneous_validation.cmake")
    set_tests_properties(cross_model_homogeneous_validation PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.aarch64;feature.model;feature.abi")
    add_test(NAME cross_model_march_modifiers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-march-modifiers
                -P "${CMAKE_SOURCE_DIR}/test/model/march_modifiers.cmake")
    set_tests_properties(cross_model_march_modifiers PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver")
    add_test(NAME cross_model_data_base_entries
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-data-base-entries
                -P "${CMAKE_SOURCE_DIR}/test/model/data_base_entries.cmake")
    set_tests_properties(cross_model_data_base_entries PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver;feature.small_data")
    add_test(NAME cross_model_init_table_entries
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/model-init-table-entries
                -P "${CMAKE_SOURCE_DIR}/test/model/init_table_entries.cmake")
    set_tests_properties(cross_model_init_table_entries PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.model;feature.driver;feature.init_tables")
    add_test(NAME cross_mangling_dsl
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/model/mangling_symbols.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mangling-dsl
                -P "${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.cmake")
    set_tests_properties(cross_mangling_dsl PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.names;feature.model;feature.generic")
    add_test(NAME cross_generic_record_mangling
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-record-mangling
                -P "${CMAKE_SOURCE_DIR}/test/model/generic_record_mangling.cmake")
    set_tests_properties(cross_generic_record_mangling PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.names;feature.model;feature.generic;feature.records")
    add_test(NAME cross_operator_binding
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/operator_binding.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/language/operator_binding_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/operator-binding.ll
                -P "${CMAKE_SOURCE_DIR}/test/language/operator_binding.cmake")
    set_tests_properties(cross_operator_binding PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.semantics")
    add_test(NAME cross_language_surface
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/language/language_surface_errors.x
                -P "${CMAKE_SOURCE_DIR}/test/language/language_surface.cmake")
    set_tests_properties(cross_language_surface PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.semantics")
    add_test(NAME cross_basic_ir
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm
                "${CMAKE_SOURCE_DIR}/test/language/basic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/basic.ll")
    set_tests_properties(cross_basic_ir PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.llvm;feature.ir")
    add_test(NAME cross_basic_object
        COMMAND $<TARGET_FILE:cross_cc> -c -O2 -ffunction-sections
                "${CMAKE_SOURCE_DIR}/test/language/basic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/basic.o")
    set_tests_properties(cross_basic_object PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.object;opt.O2")
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
            set_tests_properties(cross_gcc_gimple_bridge PROPERTIES
                LABELS "check.compile;arch.x86_64;backend.gcc")
            add_test(NAME cross_gcc_gimple_may_alias
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_may_alias.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_may_alias_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-may-alias
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_may_alias.cmake")
            set_tests_properties(cross_gcc_gimple_may_alias PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;feature.attributes;opt.O2;opt.O3")
            add_test(NAME cross_gcc_gimple_division
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_division.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_division_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-division
                        "-DLEVELS=O2;O3" -DEXPECTED=0 -DPATTERN=__MULT_HIGHPART
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
            set_tests_properties(cross_gcc_gimple_division PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;middle;feature.optimization;opt.O2;opt.O3")
            add_test(NAME cross_gcc_gimple_aligned_typedef
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_aligned_typedef.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_aligned_typedef_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-aligned-typedef
                        -DEXPECTED=1 -DPATTERN=cross_storage_
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
            set_tests_properties(cross_gcc_gimple_aligned_typedef PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;feature.layout;opt.O0;opt.O2")
            add_test(NAME cross_gcc_gimple_indirect
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_indirect.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_indirect_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-indirect
                        -DEXPECTED=0 -DPATTERN=cross_gimple_call_
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
            set_tests_properties(cross_gcc_gimple_indirect PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;feature.calls;feature.abi;opt.O0;opt.O2")
            add_test(NAME cross_gcc_gimple_vla
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_vla.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_vla_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-vla
                        -DEXPECTED=0 -DPATTERN=__builtin_alloca_with_align
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
            set_tests_properties(cross_gcc_gimple_vla PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;feature.array;feature.frame;opt.O0;opt.O2")
            add_test(NAME cross_gcc_gimple_aggregate
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DGCC=${CROSS_TEST_GCC}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_aggregate.x
                        -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_aggregate_harness.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-aggregate
                        -DEXPECTED=0
                        -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
            set_tests_properties(cross_gcc_gimple_aggregate PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.gcc;feature.initializer;feature.records;opt.O0;opt.O2")
        endif()
    endif()
    add_test(NAME cross_namespace
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/namespace.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/namespace.s")
    set_tests_properties(cross_namespace PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.names;opt.O2")
    add_test(NAME cross_readable_symbols
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/readable-symbols
                -P "${CMAKE_SOURCE_DIR}/test/model/readable_symbols.cmake")
    set_tests_properties(cross_readable_symbols PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.names")
    add_test(NAME cross_instructions
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/machine/instructions.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/instructions.s")
    set_tests_properties(cross_instructions PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw;opt.O2")
    add_test(NAME cross_machine_builtin_semantics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/instructions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/instructions.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/machine_builtins.cmake")
    set_tests_properties(cross_machine_builtin_semantics PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_naked
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-naked
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.cmake")
    set_tests_properties(cross_raw_naked PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_instruction_features
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/instruction_features.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-instruction-features
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/instruction_features.cmake")
    set_tests_properties(cross_raw_instruction_features PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/isa_families.cmake")
    set_tests_properties(cross_raw_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_base_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_families.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-base-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/base_isa_families.cmake")
    set_tests_properties(cross_raw_base_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
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
    set_tests_properties(cross_raw_simd_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;feature.raw")
    add_test(NAME cross_raw_packed_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/packed_isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-packed-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/packed_isa_families.cmake")
    set_tests_properties(cross_raw_packed_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_x87_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_families.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-x87-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/x87_isa_families.cmake")
    set_tests_properties(cross_raw_x87_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.floating;feature.raw")
    add_test(NAME cross_raw_modern_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/modern_isa_families.x
                -DQUERY_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/modern_instruction_queries.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-modern-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/modern_isa_families.cmake")
    set_tests_properties(cross_raw_modern_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_system_isa_families
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/system_isa_families.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-system-isa-families
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/system_isa_families.cmake")
    set_tests_properties(cross_raw_system_isa_families PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_naked_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_errors.x
                -DINTERFACE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_interface_errors.x
                -DCONTEXT_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_instruction_context_errors.x
                -DRECOVERY_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_parser_recovery_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-naked-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked_errors.cmake")
    set_tests_properties(cross_raw_naked_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.raw")
    set_tests_properties(cross_raw_naked_diagnostics PROPERTIES TIMEOUT 10)
    # Naked and raw_inline bodies reading, writing, and addressing
    # static-duration objects.
    foreach(raw_static_level O0 O2)
        add_test(NAME cross_runtime_raw_static_objects_${raw_static_level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_static_objects.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-raw-static-objects-${raw_static_level}
                    -DENTRY=test_entry -DEXPECTED=0
                    "-DCC_FLAGS=-${raw_static_level};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_raw_static_objects_${raw_static_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.raw;opt.${raw_static_level}")
    endforeach()
    add_test(NAME cross_raw_static_object_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_static_object_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-static-object-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_static_object_errors.cmake")
    set_tests_properties(cross_raw_static_object_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.raw")
    # RAX and RCX as ordinary allocation colors under pressure.
    foreach(scratch_mode O0 O1 O2 O3 Os Oz O2-avx2 O2-no-register-allocation)
        string(REGEX MATCH "^O[0-9sz]" scratch_level "${scratch_mode}")
        set(scratch_flags "-${scratch_level}")
        if(scratch_mode MATCHES "-avx2$")
            list(APPEND scratch_flags "-march=x86-64-v3")
        elseif(scratch_mode MATCHES "-no-(.*)$")
            list(APPEND scratch_flags "-fno-${CMAKE_MATCH_1}")
        endif()
        add_test(NAME cross_runtime_scratch_colors_${scratch_mode}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/scratch_colors.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-scratch-colors-${scratch_mode}
                    -DENTRY=test_entry -DEXPECTED=0
                    "-DCC_FLAGS=${scratch_flags};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_scratch_colors_${scratch_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;opt.${scratch_level}")
    endforeach()
    add_test(NAME cross_scratch_colors_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/scratch_colors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scratch-colors-codegen
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/scratch_colors.cmake")
    set_tests_properties(cross_scratch_colors_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization;opt.O2")
    add_test(NAME cross_raw_cfg
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-cfg
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg.cmake")
    set_tests_properties(cross_raw_cfg PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_raw_cfg_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_errors.x
                -DHIR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_hir_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-cfg-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/raw/raw_cfg_errors.cmake")
    set_tests_properties(cross_raw_cfg_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.raw")
    add_test(NAME cross_x86_64_relocation_models
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/relocation_models.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/relocation-models
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/relocation_models.cmake")
    set_tests_properties(cross_x86_64_relocation_models PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.object")
    add_test(NAME cross_x86_64_tls_models
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_models.x
                -DLARGE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_large_local.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/tls_model_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tls-models
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/tls_models.cmake")
    set_tests_properties(cross_x86_64_tls_models PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.object")
    add_test(NAME cross_x86_64_coff_unwind
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DALIGNED_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.x
                -DVLA_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                -DPROBE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/coff-unwind
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/coff_unwind.cmake")
    set_tests_properties(cross_x86_64_coff_unwind PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.object")
    # The integrated x86-64 assembler: encodings, relaxation, relocations per
    # object format, Win64 unwind tables, the as tool, and diagnostics.
    find_program(CROSS_LLVM_MC NAMES llvm-mc)
    if(CROSS_LLVM_MC)
        foreach(case encodings relaxation relocations)
            add_test(NAME cross_x86_64_assembler_${case}
                COMMAND "${CMAKE_COMMAND}"
                        -DAS=$<TARGET_FILE:cross_as>
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/${case}.s
                        -DTRIPLE=x86_64-unknown-linux-gnu
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler/${case}
                        -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/differential.cmake")
            set_tests_properties(cross_x86_64_assembler_${case} PROPERTIES
                LABELS "feature.assembler;arch.x86_64;check.compile")
        endforeach()
        add_test(NAME cross_x86_64_assembler_unwind
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/unwind.s
                    -DTRIPLE=x86_64-w64-windows-gnu
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler/unwind
                    -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/differential.cmake")
        set_tests_properties(cross_x86_64_assembler_unwind PROPERTIES
            LABELS "feature.assembler;arch.x86_64;check.compile;feature.object")
        add_test(NAME cross_x86_64_assembler_encodings_coff
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/encodings.s
                    -DTRIPLE=x86_64-w64-windows-gnu
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler/encodings-coff
                    -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/differential.cmake")
        set_tests_properties(cross_x86_64_assembler_encodings_coff PROPERTIES
            LABELS "feature.assembler;arch.x86_64;check.compile")
        add_test(NAME cross_x86_64_assembler_encodings_macho
            COMMAND "${CMAKE_COMMAND}"
                    -DAS=$<TARGET_FILE:cross_as>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/encodings.s
                    -DTRIPLE=x86_64-apple-darwin
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler/encodings-macho
                    -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/differential.cmake")
        set_tests_properties(cross_x86_64_assembler_encodings_macho PROPERTIES
            LABELS "feature.assembler;arch.x86_64;check.compile")
    endif()
    add_test(NAME cross_x86_64_assembler_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler-diagnostics
                -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/diagnostics.cmake")
    set_tests_properties(cross_x86_64_assembler_diagnostics PROPERTIES
        LABELS "feature.assembler;arch.x86_64;diagnostics")
    add_test(NAME cross_x86_64_assembler_tool
        COMMAND "${CMAKE_COMMAND}"
                -DAS=$<TARGET_FILE:cross_as>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-assembler-tool
                -P "${CMAKE_SOURCE_DIR}/test/target/x86_64/assembler/tool.cmake")
    set_tests_properties(cross_x86_64_assembler_tool PROPERTIES
        LABELS "feature.assembler;arch.x86_64;check.compile")
    add_test(NAME cross_managed_mir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/managed-mir
                -P "${CMAKE_SOURCE_DIR}/test/middle/managed_mir.cmake")
    set_tests_properties(cross_managed_mir PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.ir")
    add_test(NAME cross_control_intrinsics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/control-intrinsics
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -DGIMPLE_TEXT=${CROSS_ENABLE_GCC_GIMPLE_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/middle/control_intrinsics.cmake")
    set_tests_properties(cross_control_intrinsics PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;feature.intrinsics")
    add_test(NAME cross_data_ir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_object_errors.x
                -DALIGN_ERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_object_alignment_error.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-objects
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.cmake")
    set_tests_properties(cross_data_ir PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.ir")
    add_executable(cross_null_pointer_test test/middle/data/null_pointer_test.cpp)
    target_link_libraries(cross_null_pointer_test PRIVATE cross_middle)
    cross_component(cross_null_pointer_test)
    add_test(NAME cross_null_pointer_model COMMAND $<TARGET_FILE:cross_null_pointer_test>)
    set_tests_properties(cross_null_pointer_model PROPERTIES
        LABELS "middle;feature.initializer;feature.pointer;arch.shared;profile.custom-layout")
    add_executable(cross_evaluation_addresses_test test/middle/data/evaluation_addresses_test.cpp)
    target_link_libraries(cross_evaluation_addresses_test PRIVATE cross_middle)
    cross_component(cross_evaluation_addresses_test)
    add_executable(cross_evaluation_hir_test test/middle/evaluation_hir_test.cpp)
    target_link_libraries(cross_evaluation_hir_test PRIVATE cross_middle)
    cross_component(cross_evaluation_hir_test)
    add_test(NAME cross_evaluation_hir COMMAND $<TARGET_FILE:cross_evaluation_hir_test>)
    set_tests_properties(cross_evaluation_hir PROPERTIES
        LABELS "middle;frontend;feature.evaluation;feature.layout;feature.patch;feature.resources;arch.shared;profile.custom-layout;check.unit")
    add_test(NAME cross_evaluation_addresses COMMAND $<TARGET_FILE:cross_evaluation_addresses_test>)
    set_tests_properties(cross_evaluation_addresses PROPERTIES
        LABELS "middle;frontend;feature.evaluation;feature.pointer;feature.resources;arch.shared;profile.custom-layout;check.unit")
    add_test(NAME cross_null_pointer_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/null-pointer-errors
        -P "${CMAKE_SOURCE_DIR}/test/middle/data/null_pointer_errors.cmake")
    set_tests_properties(cross_null_pointer_errors PROPERTIES
        LABELS "frontend;feature.initializer;feature.pointer;arch.x86_64;diagnostics")
    add_test(NAME cross_initializer_overlap_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/initializer_overlap.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/initializer-overlap-errors
        -P "${CMAKE_SOURCE_DIR}/test/frontend/initializer_overlap.cmake")
    set_tests_properties(cross_initializer_overlap_errors PROPERTIES
        LABELS "frontend;feature.initializer;feature.evaluation;arch.x86_64;arch.mips;check.compile;diagnostics")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_initializer_overlap_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/initializer_overlap.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-initializer-overlap-${level}
            -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_initializer_overlap_${level} PROPERTIES
            LABELS "check.runtime;frontend;backend.native;feature.initializer;feature.evaluation;arch.x86_64;opt.${level}")
    endforeach()
    if(WIN32)
        set(null_host_abi ms_abi)
    else()
        set(null_host_abi sysv_abi)
    endif()
    foreach(pointer_fixture null_pointer void_evaluation control_intrinsic_values atomic_intrinsic_values staging_intrinsic_values syntax_type_attributes syntax_type_composition meta_unevaluated_calls qualified_results)
    set(pointer_mips_cpu r3000)
    set(pointer_mips_flags "")
    if(pointer_fixture STREQUAL null_pointer)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/middle/data/null_pointer.x")
        set(pointer_labels "middle;feature.initializer;feature.pointer;feature.syntax")
    elseif(pointer_fixture STREQUAL void_evaluation)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/middle/void_evaluation.x")
        set(pointer_labels "middle;feature.evaluation;feature.pointer;feature.syntax")
    elseif(pointer_fixture STREQUAL control_intrinsic_values)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/control_intrinsic_values.x")
        set(pointer_labels "frontend;feature.intrinsics;feature.syntax")
    elseif(pointer_fixture STREQUAL staging_intrinsic_values)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/staging_intrinsic_values.x")
        set(pointer_labels "frontend;feature.evaluation;feature.syntax;feature.meta")
    elseif(pointer_fixture STREQUAL atomic_intrinsic_values)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/atomic_intrinsic_values.x")
        set(pointer_labels "frontend;feature.atomics;feature.syntax")
        set(pointer_mips_cpu vr4300)
        set(pointer_mips_flags "-mllsc")
    elseif(pointer_fixture STREQUAL syntax_type_attributes)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/syntax_type_attributes.x")
        set(pointer_labels "frontend;feature.syntax;feature.attributes")
    elseif(pointer_fixture STREQUAL syntax_type_composition)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/syntax_type_composition.x")
        set(pointer_labels "frontend;feature.syntax;feature.types;feature.calls")
    elseif(pointer_fixture STREQUAL qualified_results)
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/middle/qualified_results.x")
        set(pointer_labels "middle;feature.syntax;feature.calls;feature.qualifiers")
    else()
        set(pointer_source "${CMAKE_SOURCE_DIR}/test/frontend/meta_unevaluated_calls.x")
        set(pointer_labels "frontend;feature.syntax;feature.calls;feature.meta")
    endif()
    foreach(level O0 O2)
        foreach(mode normal noeval custom custom_noeval)
            set(null_flags -${level})
            if(pointer_fixture STREQUAL qualified_results)
                list(APPEND null_flags -DTEST_VECTOR_RESULTS)
            endif()
            if(mode MATCHES noeval)
                list(APPEND null_flags -fno-eval-calls)
            endif()
            if(mode MATCHES custom)
                list(APPEND null_flags "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y"
                    -mabi=odd_abi -DCUSTOM_NULL_ABI "-DHOST_ABI=\"${null_host_abi}\"")
            endif()
            add_test(NAME cross_${pointer_fixture}_${level}_${mode} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${pointer_source}
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${pointer_fixture}-${level}-${mode}
                -DENTRY=syntax_raw_entry -DEXPECTED=61 "-DCC_FLAGS=${null_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_${pointer_fixture}_${level}_${mode} PROPERTIES
                LABELS "${pointer_labels};arch.x86_64;profile.${mode}")
        endforeach()
        foreach(endian big little)
            if(endian STREQUAL big)
                set(null_endian b)
            else()
                set(null_endian l)
            endif()
            add_test(NAME cross_${pointer_fixture}_o32_${endian}_${level} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${pointer_source}
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${pointer_fixture}-o32-${endian}-${level}
                -DMIPS_CPUS=${pointer_mips_cpu} -DMIPS_ENDIANS=${null_endian}
                -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls;${pointer_mips_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            add_test(NAME cross_${pointer_fixture}_n64_${endian}_${level} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${pointer_source}
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${pointer_fixture}-n64-${endian}-${level}
                -DN64_ENDIAN=${endian} "-DEXTRA_FLAGS=-${level} -fno-eval-calls ${pointer_mips_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            foreach(profile o32 n64)
                set_tests_properties(cross_${pointer_fixture}_${profile}_${endian}_${level} PROPERTIES
                    LABELS "${pointer_labels};arch.mips;profile.${profile};endian.${endian}")
            endforeach()
        endforeach()
    endforeach()
    endforeach()
    add_test(NAME cross_data_ir_string_bytes
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/string_bytes.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/string-bytes
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -DGIMPLE_TEXT=${CROSS_ENABLE_GCC_GIMPLE_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/middle/data/string_bytes.cmake")
    set_tests_properties(cross_data_ir_string_bytes PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.ir")
    add_test(NAME cross_managed_inlining
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/inlining.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/managed-inlining
                -P "${CMAKE_SOURCE_DIR}/test/middle/inlining.cmake")
    set_tests_properties(cross_managed_inlining PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.optimization")
    add_test(NAME cross_inlining_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/inlining_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/inlining-errors
                -P "${CMAKE_SOURCE_DIR}/test/middle/inlining_errors.cmake")
    set_tests_properties(cross_inlining_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;middle;feature.optimization;feature.calls")
    add_test(NAME cross_patch_inlining_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_inlining.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-inlining-errors
                -P "${CMAKE_SOURCE_DIR}/test/middle/patch_inlining.cmake")
    set_tests_properties(cross_patch_inlining_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;middle;feature.patch;feature.optimization;opt.O0;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_patch_inlining_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_inlining.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-patch-inlining-${level}
            -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_patch_inlining_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.patch;feature.optimization;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_patch_inlining_mips
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_inlining.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-patch-inlining-mips
            -DEXPECTED=P
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_patch_inlining_mips PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;middle;feature.patch;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_unreferenced_functions
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/unreferenced_functions.x
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/unreferenced-functions
                -P "${CMAKE_SOURCE_DIR}/test/middle/unreferenced_functions.cmake")
    set_tests_properties(cross_unreferenced_functions PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;backend.llvm;feature.optimization;opt.O0;opt.O2")
    add_test(NAME cross_write_only_objects
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/write_only_objects.x
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/write-only-objects
                -P "${CMAKE_SOURCE_DIR}/test/middle/write_only_objects.cmake")
    set_tests_properties(cross_write_only_objects PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;backend.llvm;feature.optimization;feature.object;feature.debug;isa.vr4300;opt.O0;opt.O2;opt.O3;opt.Os")
    if(CROSS_ENABLE_GCC_GIMPLE_TEXT AND CROSS_TEST_GCC)
        add_test(NAME cross_gcc_gimple_unreferenced_functions
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DGCC=${CROSS_TEST_GCC}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/unreferenced_functions.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/middle/unreferenced_functions_harness.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-unreferenced-functions
                "-DLEVELS=O0;O2" "-DCC_FLAGS=-DGIMPLE;-mabi=sysv_abi" -DEXPECTED=0
                -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
        set_tests_properties(cross_gcc_gimple_unreferenced_functions PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.gcc;middle;feature.optimization;opt.O0;opt.O2")
    endif()
    # Inlined control flow, output cells, and variable-length arrays under
    # mandatory, discretionary, and disabled inlining.
    foreach(variant mandatory-O0 mandatory-O2 discretionary-O2 discretionary-Os disabled-O2)
        string(REPLACE "-" ";" parts "${variant}")
        list(GET parts 0 mode)
        list(GET parts 1 level)
        if(mode STREQUAL "discretionary")
            set(define -DDISCRETIONARY)
        elseif(mode STREQUAL "disabled")
            set(define -DDISABLED)
        else()
            set(define -DMANDATORY)
        endif()
        add_test(NAME cross_inlining_runtime_${mode}_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/inlining_runtime.x
                -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/inlining_runtime_driver.cpp
                -DMODE=${mode} -DLEVEL=${level}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/inlining-runtime-${mode}-${level}
                -P "${CMAKE_SOURCE_DIR}/test/middle/inlining_runtime.cmake")
        set_tests_properties(cross_inlining_runtime_${mode}_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.calls;opt.${level}")
        foreach(endian b l)
            if(endian STREQUAL b)
                set(endian_label big)
            else()
                set(endian_label little)
            endif()
            set(name cross_inlining_runtime_o32_vr4300_${endian}_${mode}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/inlining_runtime.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/inlining_runtime_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/inlining-runtime-o32-${endian}-${mode}-${level}
                -DMIPS_CPUS=vr4300 -DMIPS_ENDIANS=${endian} -DMIPS_SINGLE_CASE=ON
                -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls;${define}"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.calls;profile.o32;isa.vr4300;endian.${endian_label};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_call_cloning
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/call_cloning.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/call-cloning
                -P "${CMAKE_SOURCE_DIR}/test/middle/call_cloning.cmake")
    set_tests_properties(cross_call_cloning PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.optimization")
    add_test(NAME cross_optimization_flags
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/optimization_flags.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/optimization-flags
                -P "${CMAKE_SOURCE_DIR}/test/middle/optimization_flags.cmake")
    set_tests_properties(cross_optimization_flags PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.optimization")
    add_test(NAME cross_division_by_constant_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/division_by_constant.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/division-by-constant
                -P "${CMAKE_SOURCE_DIR}/test/middle/division_by_constant.cmake")
    set_tests_properties(cross_division_by_constant_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization")
    foreach(division_mode O0 O2 O3 Os O0-enabled O2-disabled)
        if(division_mode STREQUAL "O0-enabled")
            set(division_flags "-O0;-fdiv-by-constant")
            set(division_level O0)
        elseif(division_mode STREQUAL "O2-disabled")
            set(division_flags "-O2;-fno-div-by-constant")
            set(division_level O2)
        else()
            set(division_flags "-${division_mode}")
            set(division_level ${division_mode})
        endif()
        add_test(NAME cross_runtime_division_by_constant_${division_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/division_by_constant.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-division-${division_mode}
                -DENTRY=division_entry -DEXPECTED=0
                "-DCC_FLAGS=${division_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_division_by_constant_${division_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${division_level}")
    endforeach()
    foreach(division_mode O2 Os O0-enabled)
        if(division_mode STREQUAL "O0-enabled")
            set(division_flags "-O0;-fdiv-by-constant")
            set(division_level O0)
        else()
            set(division_flags "-${division_mode}")
            set(division_level ${division_mode})
        endif()
        add_test(NAME cross_runtime_division_by_constant_mips_${division_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/division_by_constant.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/division_by_constant_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-division-mips-${division_mode}
                -DEXPECTED=P "-DCC_FLAGS=${division_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_division_by_constant_mips_${division_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;opt.${division_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_runtime_division_by_constant_llvm
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCC_FLAGS=-O2
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/division_by_constant.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-division-llvm.ll
                -DENTRY=division_entry
                -DEXPECTED=0
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_division_by_constant_llvm PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;middle;feature.optimization;opt.O2")
    # Multiplication by a constant: shifts and additions where the multiply
    # costs more, at the RISC end of the cost balance and with a one-cycle
    # multiply that keeps the instruction.
    set(multiplication_model ${CMAKE_SOURCE_DIR}/test/middle/fast_multiply.xm)
    foreach(multiplication_mode O0 O2 O2-risc Os-risc O2-fastmul)
        if(multiplication_mode STREQUAL "O2-risc")
            set(multiplication_flags "-O2;-mrisc-cisc-balance=0")
            set(multiplication_level O2)
        elseif(multiplication_mode STREQUAL "Os-risc")
            set(multiplication_flags "-Os;-mrisc-cisc-balance=0")
            set(multiplication_level Os)
        elseif(multiplication_mode STREQUAL "O2-fastmul")
            set(multiplication_flags
                "-O2;-mrisc-cisc-balance=0;--model=${multiplication_model};-mtune=fastmul")
            set(multiplication_level O2)
        else()
            set(multiplication_flags "-${multiplication_mode}")
            set(multiplication_level ${multiplication_mode})
        endif()
        add_test(NAME cross_runtime_multiplication_by_constant_${multiplication_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/multiplication_by_constant.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-multiplication-${multiplication_mode}
                -DENTRY=multiplication_entry -DEXPECTED=0
                "-DCC_FLAGS=${multiplication_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_multiplication_by_constant_${multiplication_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.model;opt.${multiplication_level}")
    endforeach()
    foreach(multiplication_mode O2 Os O2-fastmul)
        if(multiplication_mode STREQUAL "O2-fastmul")
            set(multiplication_flags
                "-O2;--model=${multiplication_model};-mtune=fastmul")
            set(multiplication_level O2)
        else()
            set(multiplication_flags "-${multiplication_mode}")
            set(multiplication_level ${multiplication_mode})
        endif()
        add_test(NAME cross_runtime_multiplication_by_constant_mips_${multiplication_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/multiplication_by_constant.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/multiplication_by_constant_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-multiplication-mips-${multiplication_mode}
                -DEXPECTED=P "-DCC_FLAGS=${multiplication_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_multiplication_by_constant_mips_${multiplication_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.model;isa.vr4300;endian.big;endian.little;opt.${multiplication_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Carried constant products and modular recurrence reassociation.
    foreach(recurrence_mode O0 O2 O3 O2-v3 O3-v3 Os no-slsr no-reassoc fastmul slowshift slowmul slowadd slowsub)
        set(recurrence_flags "-O2;-fwrapv;-fno-eval-calls")
        set(recurrence_level O2)
        if(recurrence_mode MATCHES "^(O0|O2|O3|Os)$")
            set(recurrence_flags "-${recurrence_mode};-fwrapv;-fno-eval-calls")
            set(recurrence_level ${recurrence_mode})
        elseif(recurrence_mode MATCHES "^(O2|O3)-v3$")
            set(recurrence_level ${CMAKE_MATCH_1})
            set(recurrence_flags "-${recurrence_level};-fwrapv;-fno-eval-calls;-march=x86-64-v3")
        elseif(recurrence_mode STREQUAL "no-slsr")
            list(APPEND recurrence_flags -fno-tree-slsr)
        elseif(recurrence_mode STREQUAL "no-reassoc")
            list(APPEND recurrence_flags -fno-tree-reassoc)
        elseif(recurrence_mode STREQUAL "fastmul")
            list(APPEND recurrence_flags "--model=${multiplication_model}" -mtune=fastmul)
        elseif(recurrence_mode MATCHES "^slow(shift|mul|add|sub)$")
            list(APPEND recurrence_flags
                "--model=${CMAKE_SOURCE_DIR}/test/middle/recurrence_costs.xm"
                -mtune=${recurrence_mode})
        endif()
        add_test(NAME cross_runtime_constant_recurrences_${recurrence_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/constant_recurrences.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-recurrences-${recurrence_mode}
                -DENTRY=recurrence_entry -DEXPECTED=0
                "-DCC_FLAGS=-target;x86_64-unknown-linux-gnu;${recurrence_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_constant_recurrences_${recurrence_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.model;opt.${recurrence_level}")
    endforeach()
    foreach(recurrence_cpu vr4300 r3000)
        foreach(recurrence_endian b l)
            if(recurrence_endian STREQUAL "b")
                set(recurrence_byte_order big)
            else()
                set(recurrence_byte_order little)
            endif()
            foreach(recurrence_level O2 O3)
                set(recurrence_name ${recurrence_cpu}_${recurrence_endian}_${recurrence_level})
                add_test(NAME cross_runtime_constant_recurrences_${recurrence_name}
                    COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/constant_recurrences.x
                        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/constant_recurrences_start.s
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-recurrences-${recurrence_name}
                        -DEXPECTED=P "-DCC_FLAGS=-${recurrence_level};-fwrapv;-fno-eval-calls"
                        -DMIPS_CPUS=${recurrence_cpu} -DMIPS_ENDIANS=${recurrence_endian}
                        -DMIPS_SINGLE_CASE=ON
                        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                set_tests_properties(cross_runtime_constant_recurrences_${recurrence_name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.${recurrence_cpu};endian.${recurrence_byte_order};opt.${recurrence_level}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()
    foreach(recurrence_arch x86_64 mips)
        add_test(NAME cross_constant_recurrences_codegen_${recurrence_arch}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/constant_recurrences.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/recurrence_costs.xm
                -DFAST_MODEL=${multiplication_model}
                -DARCH=${recurrence_arch}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/recurrences-codegen-${recurrence_arch}
                -P "${CMAKE_SOURCE_DIR}/test/middle/constant_recurrences.cmake")
        set_tests_properties(cross_constant_recurrences_codegen_${recurrence_arch} PROPERTIES
            LABELS "check.codegen;arch.${recurrence_arch};backend.native;middle;feature.optimization;feature.model")
    endforeach()
    # Floating constant folding, constant-object reads, memory value
    # forwarding, conditional replacement, and shared store addresses.
    add_test(NAME cross_mir_folding_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/mir_folding_codegen.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/floating_environment/environments.xm
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mir-folding-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/mir_folding_codegen.cmake")
    set_tests_properties(cross_mir_folding_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;feature.floating;feature.model;isa.vr4300;opt.O2")
    foreach(folding_test floating_folding floating_folding_wide memory_forwarding
                         conditional_replacement)
        string(REPLACE "_" "-" folding_name "${folding_test}")
        if(folding_test STREQUAL "floating_folding_wide")
            # f80 and f128 arithmetic exist only on x86-64.
            set(folding_modes O0 O2 O2-no-tree-ccp)
            set(folding_mips_modes "")
            set(folding_labels "feature.floating")
        elseif(folding_test STREQUAL "floating_folding")
            set(folding_modes O0 O2 O3 O2-no-tree-ccp O2-no-div-by-constant)
            set(folding_mips_modes O2 O2-no-tree-ccp)
            # The R3000 has no square root.
            set(folding_cpus vr4300)
            set(folding_labels "feature.floating")
        elseif(folding_test STREQUAL "memory_forwarding")
            set(folding_modes O0 O2 O3 Os O2-no-tree-fre O2-no-tree-ccp O2-no-ivopts)
            set(folding_mips_modes O2 O2-no-tree-fre O2-no-ivopts)
            set(folding_cpus vr4300 r3000)
            set(folding_labels "feature.pointer;feature.object")
        else()
            set(folding_modes O0 O2 O2-no-tree-ccp)
            set(folding_mips_modes O2 O2-no-tree-ccp)
            set(folding_cpus vr4300 r3000)
            set(folding_labels "feature.control")
        endif()
        foreach(folding_mode IN LISTS folding_modes)
            string(REGEX MATCH "^O[0-9s]" folding_level "${folding_mode}")
            if(folding_mode MATCHES "^O[0-9s]-no-(.*)$")
                set(folding_flags "-${folding_level};-fno-${CMAKE_MATCH_1}")
            else()
                set(folding_flags "-${folding_mode}")
            endif()
            # Calls run at run time, so the optimizer, not translation-time
            # evaluation, sees their constant operands.
            list(APPEND folding_flags -fno-eval-calls)
            add_test(NAME cross_runtime_${folding_test}_${folding_mode}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/${folding_test}.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-${folding_name}-${folding_mode}
                    -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${folding_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_${folding_test}_${folding_mode} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;${folding_labels};opt.${folding_level}")
        endforeach()
        foreach(folding_mode IN LISTS folding_mips_modes)
            string(REGEX MATCH "^O[0-9s]" folding_level "${folding_mode}")
            if(folding_mode MATCHES "^O[0-9s]-no-(.*)$")
                set(folding_flags "-${folding_level};-fno-${CMAKE_MATCH_1}")
            else()
                set(folding_flags "-${folding_mode}")
            endif()
            # Calls run at run time, so the optimizer, not translation-time
            # evaluation, sees their constant operands.
            list(APPEND folding_flags -fno-eval-calls)
            set(folding_isa_labels "")
            foreach(folding_cpu IN LISTS folding_cpus)
                string(APPEND folding_isa_labels ";isa.${folding_cpu}")
            endforeach()
            add_test(NAME cross_runtime_${folding_test}_mips_${folding_mode}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/${folding_test}.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-${folding_name}-mips-${folding_mode}
                    -DEXPECTED=P "-DCC_FLAGS=${folding_flags}"
                    "-DMIPS_CPUS=${folding_cpus}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_runtime_${folding_test}_mips_${folding_mode} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;${folding_labels}${folding_isa_labels};endian.big;endian.little;opt.${folding_level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # Volatile parameter cells, and deduction from volatile lvalues.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_volatile_parameters_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/volatile_parameters.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-volatile-parameters-${level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_volatile_parameters_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.parameters;feature.generic;opt.${level}")
        add_test(NAME cross_runtime_volatile_parameters_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/volatile_parameters.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-volatile-parameters-mips-${level}
                -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_volatile_parameters_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.parameters;feature.generic;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Stores to fixed locations that leave loops for the loop exits.
    foreach(motion_mode O0 O1 O2 O3 Os O2-no-move-loop-invariants O2-no-tree-loop-rotate)
        string(REGEX MATCH "^O[0-9s]" motion_level "${motion_mode}")
        if(motion_mode MATCHES "^O[0-9s]-no-(.*)$")
            set(motion_flags "-${motion_level};-fno-${CMAKE_MATCH_1}")
        else()
            set(motion_flags "-${motion_mode}")
        endif()
        add_test(NAME cross_runtime_store_motion_${motion_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/store_motion.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-store-motion-${motion_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${motion_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_store_motion_${motion_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.object;opt.${motion_level}")
    endforeach()
    foreach(motion_mode O2 O3 Os O2-no-move-loop-invariants)
        string(REGEX MATCH "^O[0-9s]" motion_level "${motion_mode}")
        if(motion_mode MATCHES "^O[0-9s]-no-(.*)$")
            set(motion_flags "-${motion_level};-fno-${CMAKE_MATCH_1}")
        else()
            set(motion_flags "-${motion_mode}")
        endif()
        add_test(NAME cross_runtime_store_motion_mips_${motion_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/store_motion.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-store-motion-mips-${motion_mode}
                -DEXPECTED=P "-DCC_FLAGS=${motion_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_store_motion_mips_${motion_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.object;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${motion_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_store_motion_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/store_motion_codegen.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/floating_environment/environments.xm
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/store-motion-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/store_motion_codegen.cmake")
    set_tests_properties(cross_store_motion_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;feature.object;feature.floating;feature.model;isa.vr4300;opt.O2;opt.O3;opt.Os")
    if(WIN32)
        set(perf_target x86_64-w64-windows-gnu)
    else()
        set(perf_target x86_64-unknown-linux-gnu)
    endif()
    foreach(perf_source narrow_loads runtime_integer_reduction)
        if(perf_source STREQUAL "narrow_loads")
            set(perf_path test/backend/machine/narrow_loads.x)
            set(perf_name narrow_loads)
            set(perf_labels "backend.machine;feature.memory")
        else()
            set(perf_path test/middle/runtime_integer_reduction.x)
            set(perf_name integer_reduction)
            set(perf_labels "middle;feature.vector")
        endif()
        foreach(perf_mode O0 O1 O2 O3 Os Oz O2-avx2 O3-avx2 O2-no-vectorize
                          O2-no-combine O2-no-fold O2-no-fusion O2-no-thread O2-wrapv)
            string(REGEX MATCH "^O[0-9sz]" perf_level "${perf_mode}")
            set(perf_flags "-${perf_level};-fno-eval-calls;-target;${perf_target}")
            if(perf_mode MATCHES "avx2")
                list(APPEND perf_flags -march=x86-64-v3)
            elseif(perf_mode MATCHES "no-vectorize")
                list(APPEND perf_flags -fno-tree-loop-vectorize -fno-tree-slp-vectorize)
            elseif(perf_mode MATCHES "no-combine")
                list(APPEND perf_flags -fno-machine-combine)
            elseif(perf_mode MATCHES "no-fold")
                list(APPEND perf_flags -fno-fold-memory-operands)
            elseif(perf_mode MATCHES "no-fusion")
                list(APPEND perf_flags -fno-compare-branch-fusion)
            elseif(perf_mode MATCHES "no-thread")
                list(APPEND perf_flags -fno-thread-jumps)
            elseif(perf_mode MATCHES "wrapv")
                list(APPEND perf_flags -fwrapv)
            endif()
            add_test(NAME cross_runtime_${perf_name}_${perf_mode}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/${perf_path}
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${perf_source}-${perf_mode}
                    -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${perf_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_${perf_name}_${perf_mode} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;${perf_labels};feature.optimization;opt.${perf_level}")
        endforeach()
        foreach(perf_level O2 O3)
            add_test(NAME cross_runtime_${perf_name}_mips_${perf_level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/${perf_path}
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${perf_source}-mips-${perf_level}
                    -DEXPECTED=P "-DCC_FLAGS=-${perf_level};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_runtime_${perf_name}_mips_${perf_level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;${perf_labels};feature.optimization;opt.${perf_level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_narrow_loads_codegen
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/narrow_loads.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/narrow_loads-codegen
            -P "${CMAKE_SOURCE_DIR}/test/backend/machine/narrow_loads.cmake")
    set_tests_properties(cross_narrow_loads_codegen PROPERTIES
        LABELS "check.codegen;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_runtime_integer_reduction_codegen
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/runtime_integer_reduction.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime_integer_reduction-codegen
            -P "${CMAKE_SOURCE_DIR}/test/middle/runtime_integer_reduction.cmake")
    set_tests_properties(cross_runtime_integer_reduction_codegen PROPERTIES
        LABELS "check.codegen;arch.x86_64;backend.native;feature.optimization")
    # Conditional reductions over floating and narrowed index comparisons.
    foreach(reduction_mode O0 O2 O2-avx2 O3 O3-avx2)
        string(REGEX MATCH "^O[0-9s]" reduction_level "${reduction_mode}")
        set(reduction_flags "-${reduction_level}")
        if(reduction_mode MATCHES "-avx2$")
            list(APPEND reduction_flags "-march=x86-64-v3")
        endif()
        add_test(NAME cross_runtime_conditional_reductions_${reduction_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/conditional_reductions.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-conditional-reductions-${reduction_mode}
                -DENTRY=test_entry -DEXPECTED=0
                "-DCC_FLAGS=${reduction_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_conditional_reductions_${reduction_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.vector;opt.${reduction_level}")
    endforeach()
    add_test(NAME cross_conditional_reduction_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/conditional_reductions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/conditional-reduction-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/conditional_reductions.cmake")
    set_tests_properties(cross_conditional_reduction_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;middle;feature.optimization;feature.vector;opt.O3")
    # Loop bounds computed inside a vectorized reduction loop.
    foreach(level O0 O1 O2 O3 Os Oz)
        add_test(NAME cross_runtime_loop_invariant_bounds_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_invariant_bounds.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-bounds-${level}
                -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_loop_invariant_bounds_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.vector;opt.${level}")
        add_test(NAME cross_runtime_loop_invariant_bounds_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_invariant_bounds.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-bounds-mips-${level}
                -DEXPECTED=P -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_loop_invariant_bounds_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Complete peeling of loops with a constant trip count, or bounded by
    # the extent of an array they search.
    foreach(peel_mode O0 O1-peel-loops O2 O3 Os O2-no-peel-loops O2-peel-limit
                      O2-no-loop-bounds)
        string(REGEX MATCH "^O[0-9s]" peel_level "${peel_mode}")
        if(peel_mode STREQUAL "O1-peel-loops")
            set(peel_flags "-O1;-fpeel-loops;-fpeel-limit=96")
        elseif(peel_mode STREQUAL "O2-no-peel-loops")
            set(peel_flags "-O2;-fno-peel-loops")
        elseif(peel_mode STREQUAL "O2-no-loop-bounds")
            set(peel_flags "-O2;-fno-loop-bounds-from-extents")
        elseif(peel_mode STREQUAL "O2-peel-limit")
            set(peel_flags "-O2;-fpeel-limit=4096")
        else()
            set(peel_flags "-${peel_mode}")
        endif()
        add_test(NAME cross_runtime_loop_peeling_${peel_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_peeling.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-peeling-${peel_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${peel_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_loop_peeling_${peel_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${peel_level}")
    endforeach()
    foreach(peel_mode O2 O3 O2-no-peel-loops O2-no-loop-bounds)
        string(REGEX MATCH "^O[0-9s]" peel_level "${peel_mode}")
        if(peel_mode STREQUAL "O2-no-peel-loops")
            set(peel_flags "-O2;-fno-peel-loops")
        elseif(peel_mode STREQUAL "O2-no-loop-bounds")
            set(peel_flags "-O2;-fno-loop-bounds-from-extents")
        else()
            set(peel_flags "-${peel_mode}")
        endif()
        add_test(NAME cross_runtime_loop_peeling_mips_${peel_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_peeling.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-peeling-mips-${peel_mode}
                -DEXPECTED=P "-DCC_FLAGS=${peel_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_loop_peeling_mips_${peel_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${peel_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_loop_peeling_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_peeling_codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/loop-peeling-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/loop_peeling_codegen.cmake")
    set_tests_properties(cross_loop_peeling_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;isa.vr4300;opt.O1;opt.O2;opt.O3;opt.Os")
    # Branches that value bounds decide on every path to them, or on the
    # path from one edge, folded and threaded.
    foreach(thread_mode O0 O2 O3 Os Oz O2-no-thread-jumps O3-thread-jumps-limit)
        string(REGEX MATCH "^O[0-9sz]" thread_level "${thread_mode}")
        if(thread_mode STREQUAL "O2-no-thread-jumps")
            set(thread_flags "-O2;-fno-thread-jumps")
        elseif(thread_mode STREQUAL "O3-thread-jumps-limit")
            set(thread_flags "-O3;-fthread-jumps-limit=64")
        else()
            set(thread_flags "-${thread_mode}")
        endif()
        add_test(NAME cross_runtime_jump_threading_${thread_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/jump_threading.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-jump-threading-${thread_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${thread_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_jump_threading_${thread_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${thread_level}")
    endforeach()
    foreach(thread_mode O2 O3 Oz O2-no-thread-jumps O3-thread-jumps-limit)
        string(REGEX MATCH "^O[0-9sz]" thread_level "${thread_mode}")
        if(thread_mode STREQUAL "O2-no-thread-jumps")
            set(thread_flags "-O2;-fno-thread-jumps")
        elseif(thread_mode STREQUAL "O3-thread-jumps-limit")
            set(thread_flags "-O3;-fthread-jumps-limit=64")
        else()
            set(thread_flags "-${thread_mode}")
        endif()
        add_test(NAME cross_runtime_jump_threading_mips_${thread_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/jump_threading.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-jump-threading-mips-${thread_mode}
                -DEXPECTED=P "-DCC_FLAGS=${thread_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_jump_threading_mips_${thread_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${thread_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Values live across a call whose ABI preserves t0/t1 stay out of the
    # registers that compose its result.
    add_test(NAME cross_runtime_mips_low_scratch_calls
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/low_scratch_calls.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-mips-low-scratch-calls
            -DEXPECTED=P "-DCC_FLAGS=-O2;-fno-eval-calls;--model=${CMAKE_SOURCE_DIR}/test/target/mips/low_scratch_calls.xm"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_mips_low_scratch_calls PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.model;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_jump_threading_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/jump_threading_codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/jump-threading-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/jump_threading_codegen.cmake")
    set_tests_properties(cross_jump_threading_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;isa.vr4300;opt.O2;opt.O3;opt.Os;opt.Oz")
    # Floating selects from if-conversion: SSE logic, SSE4.1 BLENDV, and AVX
    # VBLENDV masks, a boolean mask, and the branches they replace.
    foreach(select_mode O0 O2 O3 Os O2-sse41 O2-avx O3-avx O2-no-if-conversion
                        O2-no-compare-select-fusion)
        string(REGEX MATCH "^O[0-9s]" select_level "${select_mode}")
        if(select_mode STREQUAL "O2-sse41")
            set(select_flags "-O2;-march=x86-64-v2")
        elseif(select_mode MATCHES "^(O[0-9])-avx$")
            set(select_flags "-${CMAKE_MATCH_1};-march=x86-64-v3")
        elseif(select_mode MATCHES "^O[0-9s]-no-(.*)$")
            set(select_flags "-${select_level};-fno-${CMAKE_MATCH_1}")
        else()
            set(select_flags "-${select_mode}")
        endif()
        add_test(NAME cross_runtime_floating_select_${select_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_select.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-select-${select_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${select_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_select_${select_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.floating;opt.${select_level}")
    endforeach()
    foreach(select_level O2 O3)
        add_test(NAME cross_runtime_floating_select_mips_${select_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_select.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-select-mips-${select_level}
                -DEXPECTED=P "-DCC_FLAGS=-${select_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_floating_select_mips_${select_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.floating;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${select_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # MIPS IV and MIPS32 select floating values with MOVT/MOVN.fmt; the MIPS
    # profiles leave ordinary if-conversion off, so the tests set a budget.
    foreach(select_mode mips4-O2 mips4-O3 mips4-O2-no-compare-select-fusion
                        mips32-O2)
        string(REGEX MATCH "^(mips[0-9]+)-(O[0-9])" select_match "${select_mode}")
        set(select_cpu "${CMAKE_MATCH_1}")
        set(select_level "${CMAKE_MATCH_2}")
        set(select_flags "-${select_level};-fif-conversion-limit=6;-fno-eval-calls")
        if(select_mode MATCHES "-no-(.*)$")
            list(APPEND select_flags "-fno-${CMAKE_MATCH_1}")
        endif()
        if(select_cpu STREQUAL "mips4")
            # The startup and transport wrapper use the o32 FP32 registers.
            list(APPEND select_flags "-mno-fp64" "-mfp32")
        endif()
        add_test(NAME cross_runtime_floating_select_${select_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_select.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-select-${select_mode}
                -DEXPECTED=P "-DCC_FLAGS=${select_flags}"
                -DMIPS_CPUS=${select_cpu} -DQEMU_CPU=MIPS64R2-generic
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_floating_select_${select_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.floating;isa.${select_cpu};endian.big;endian.little;opt.${select_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Reductions whose packed operations a CPU may lack.
    foreach(packed_mode O2 O2-sse41 O2-avx2 O3-avx2)
        string(REGEX MATCH "^O[0-9]" packed_level "${packed_mode}")
        set(packed_flags "-${packed_level}")
        if(packed_mode MATCHES "-sse41$")
            list(APPEND packed_flags "-march=x86-64-v2")
        elseif(packed_mode MATCHES "-avx2$")
            list(APPEND packed_flags "-march=x86-64-v3")
        endif()
        add_test(NAME cross_runtime_vector_packed_operations_${packed_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/vector_packed_operations.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-vector-packed-operations-${packed_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${packed_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_vector_packed_operations_${packed_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.vector;opt.${packed_level}")
    endforeach()
    add_test(NAME cross_vector_packed_operations_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/vector_packed_operations.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-packed-operations-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/vector_packed_operations.cmake")
    set_tests_properties(cross_vector_packed_operations_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;backend.native;feature.optimization;feature.vector;opt.O2")
    # Stores into local unions and records that nothing observes.
    foreach(store_level O0 O1 O2 O3 Os)
        add_test(NAME cross_runtime_dead_local_stores_${store_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/dead_local_stores.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-dead-local-stores-${store_level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${store_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_dead_local_stores_${store_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${store_level}")
    endforeach()
    foreach(store_level O2 O3)
        add_test(NAME cross_runtime_dead_local_stores_mips_${store_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/dead_local_stores.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-dead-local-stores-mips-${store_level}
                -DEXPECTED=P "-DCC_FLAGS=-${store_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_dead_local_stores_mips_${store_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${store_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_dead_local_stores_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/dead_local_stores.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dead-local-stores-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/dead_local_stores.cmake")
    set_tests_properties(cross_dead_local_stores_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;isa.vr4300;opt.O2")
    # Conditions joined by && and || that combine into one branch or none.
    foreach(combine_mode O0 O2 O3 Os O3-avx2 O3-no-if-conversion)
        string(REGEX MATCH "^O[0-9s]" combine_level "${combine_mode}")
        set(combine_flags "-${combine_level}")
        if(combine_mode MATCHES "-avx2$")
            list(APPEND combine_flags "-march=x86-64-v3")
        elseif(combine_mode MATCHES "-no-(.*)$")
            list(APPEND combine_flags "-fno-${CMAKE_MATCH_1}")
        endif()
        add_test(NAME cross_runtime_branch_combining_${combine_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/branch_combining.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-branch-combining-${combine_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${combine_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_branch_combining_${combine_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${combine_level}")
    endforeach()
    foreach(combine_level O2 O3)
        add_test(NAME cross_runtime_branch_combining_mips_${combine_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/branch_combining.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-branch-combining-mips-${combine_level}
                -DEXPECTED=P "-DCC_FLAGS=-${combine_level};-fif-conversion-limit=12;-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_branch_combining_mips_${combine_level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${combine_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_branch_combining_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/branch_combining.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/branch-combining-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/branch_combining.cmake")
    set_tests_properties(cross_branch_combining_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;backend.native;feature.optimization;opt.O3")
    # Tests of one integer joined by && or || that fold into one comparison.
    foreach(range_level O0 O1 O2 O3 Os)
        add_test(NAME cross_runtime_range_tests_${range_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/range_tests.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-range-tests-${range_level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${range_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_range_tests_${range_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${range_level}")
    endforeach()
    add_test(NAME cross_runtime_range_tests_mips_O2
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/range_tests.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-range-tests-mips-O2
            -DEXPECTED=P "-DCC_FLAGS=-O2;-fno-eval-calls"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_range_tests_mips_O2 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    # Bounded index additions that join address displacements.
    foreach(bounded_level O0 O2 O3 Os)
        add_test(NAME cross_runtime_bounded_indices_${bounded_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/bounded_indices.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bounded-indices-${bounded_level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${bounded_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_bounded_indices_${bounded_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${bounded_level}")
    endforeach()
    add_test(NAME cross_runtime_bounded_indices_mips_O2
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/bounded_indices.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bounded-indices-mips-O2
            -DEXPECTED=P "-DCC_FLAGS=-O2;-fno-eval-calls"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_bounded_indices_mips_O2 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_bounded_indices_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/bounded_indices.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bounded-indices-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/bounded_indices.cmake")
    set_tests_properties(cross_bounded_indices_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;backend.native;feature.optimization;opt.O2")
    # Masks, sets, and offsets whose bits no user reads.
    foreach(demanded_level O0 O1 O2 O3 Os)
        add_test(NAME cross_runtime_demanded_bits_${demanded_level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/demanded_bits.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-demanded-bits-${demanded_level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${demanded_level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_demanded_bits_${demanded_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${demanded_level}")
    endforeach()
    add_test(NAME cross_runtime_demanded_bits_mips_O2
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/demanded_bits.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-demanded-bits-mips-O2
            -DEXPECTED=P "-DCC_FLAGS=-O2;-fno-eval-calls"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_demanded_bits_mips_O2 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_demanded_bits_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/demanded_bits.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/demanded-bits-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/demanded_bits.cmake")
    set_tests_properties(cross_demanded_bits_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;backend.native;feature.optimization;opt.O2")
    add_test(NAME cross_range_tests_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/range_tests.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/range-tests-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/range_tests.cmake")
    set_tests_properties(cross_range_tests_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;backend.native;feature.optimization;opt.O2")
    add_test(NAME cross_floating_select_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_select_codegen.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/floating_environment/environments.xm
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-select-codegen
                -P "${CMAKE_SOURCE_DIR}/test/middle/floating_select_codegen.cmake")
    set_tests_properties(cross_floating_select_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.optimization;feature.floating;feature.model;isa.mips4;isa.vr4300;opt.O0;opt.O2")
    add_test(NAME cross_x86_64_shared_edge_copies
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_exits.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shared-edge-copies
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/edge_copies.cmake")
    set_tests_properties(cross_x86_64_shared_edge_copies PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization;feature.control;opt.O2")
    # Loop exits that carry work and phi copies after block layout.
    foreach(exit_mode O2 O3 O3-unroll O3-no-reorder-blocks
                      O3-no-schedule-insns2)
        string(REGEX MATCH "^O[0-9s]" exit_level "${exit_mode}")
        set(exit_flags "-${exit_level}")
        if(exit_mode MATCHES "-unroll$")
            list(APPEND exit_flags "-funroll-loops" "-funroll-factor=4")
        elseif(exit_mode MATCHES "-no-(.*)$")
            list(APPEND exit_flags "-fno-${CMAKE_MATCH_1}")
        endif()
        add_test(NAME cross_runtime_loop_exits_${exit_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_exits.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-exits-${exit_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${exit_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_loop_exits_${exit_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;feature.control;opt.${exit_level}")
        add_test(NAME cross_runtime_loop_exits_mips_${exit_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_exits.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-exits-mips-${exit_mode}
                -DEXPECTED=P "-DCC_FLAGS=${exit_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_loop_exits_mips_${exit_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.optimization;feature.control;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${exit_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Unsigned-to-floating conversions of values with a clear top bit.
    foreach(conversion_mode O0 O2 Os O2-no-tree-bit-ccp)
        string(REGEX MATCH "^O[0-9s]" conversion_level "${conversion_mode}")
        if(conversion_mode MATCHES "^O[0-9s]-no-(.*)$")
            set(conversion_flags "-${conversion_level};-fno-${CMAKE_MATCH_1}")
        else()
            set(conversion_flags "-${conversion_mode}")
        endif()
        add_test(NAME cross_runtime_nonnegative_conversion_${conversion_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/nonnegative_conversion.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-nonnegative-conversion-${conversion_mode}
                -DENTRY=test_entry -DEXPECTED=0
                "-DCC_FLAGS=${conversion_flags};-fno-eval-calls;-DTEST_U64"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_nonnegative_conversion_${conversion_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.floating;opt.${conversion_level}")
        add_test(NAME cross_runtime_nonnegative_conversion_mips_${conversion_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/nonnegative_conversion.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-nonnegative-conversion-mips-${conversion_mode}
                -DEXPECTED=P "-DCC_FLAGS=${conversion_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_nonnegative_conversion_mips_${conversion_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.floating;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${conversion_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Loops whose index only counts passes test the array pointer instead.
    foreach(pointer_mode O2 O3 Os O2-no-ivopts)
        string(REGEX MATCH "^O[0-9s]" pointer_level "${pointer_mode}")
        if(pointer_mode MATCHES "^O[0-9s]-no-(.*)$")
            set(pointer_flags "-${pointer_level};-fno-${CMAKE_MATCH_1}")
        else()
            set(pointer_flags "-${pointer_mode}")
        endif()
        add_test(NAME cross_runtime_pointer_exit_mips_${pointer_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/pointer_exit.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-pointer-exit-mips-${pointer_mode}
                -DEXPECTED=P "-DCC_FLAGS=${pointer_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_pointer_exit_mips_${pointer_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;feature.pointer;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${pointer_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # The RISC end of the cost coordinate forms pointer inductions on x86-64.
    foreach(pointer_mode O2 O2-risc O3-risc)
        string(REGEX MATCH "^O[0-9s]" pointer_level "${pointer_mode}")
        set(pointer_flags "-${pointer_level}")
        if(pointer_mode MATCHES "-risc$")
            list(APPEND pointer_flags -mrisc-cisc-balance=0)
        endif()
        add_test(NAME cross_runtime_pointer_exit_${pointer_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/pointer_exit.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-pointer-exit-${pointer_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${pointer_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_pointer_exit_${pointer_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;feature.pointer;opt.${pointer_level}")
    endforeach()
    # Size presets derive a scaled induction from the unit counter.
    foreach(level Os Oz O2)
        add_test(NAME cross_runtime_scaled_induction_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/scaled_induction.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-scaled-induction-${level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_scaled_induction_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${level}")
    endforeach()
    foreach(level Os Oz)
        add_test(NAME cross_runtime_scaled_induction_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/scaled_induction.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-scaled-induction-mips-${level}
                -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls" -DMIPS_CPUS=vr4300
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_scaled_induction_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Array bounds and enumerators that query a bound inferred from an initializer.
    foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
        string(REGEX REPLACE "-.*" "" architecture "${target}")
        add_test(NAME cross_inferred_bound_queries_${architecture}
            COMMAND $<TARGET_FILE:cross_cc> -target ${target} -S
                    "${CMAKE_SOURCE_DIR}/test/language/inferred_bounds.x"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/inferred-bounds-${architecture}.s")
    endforeach()
    set_tests_properties(cross_inferred_bound_queries_x86_64 PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.array;feature.evaluation;feature.enum")
    set_tests_properties(cross_inferred_bound_queries_mips PROPERTIES
        LABELS "check.compile;arch.mips;frontend;feature.array;feature.evaluation;feature.enum")
    # Static pointers initialized from integers.
    add_test(NAME cross_integer_pointer_initializers
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/integer_pointers.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/integer-pointers
            -P "${CMAKE_SOURCE_DIR}/test/middle/data/integer_pointers.cmake")
    set_tests_properties(cross_integer_pointer_initializers PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.initializer;feature.pointer")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_integer_pointer_initializers_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/integer_pointers.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-integer-pointers-${level}
                -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_integer_pointer_initializers_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.initializer;feature.pointer;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_integer_pointer_initializers_mips
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/integer_pointers.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-integer-pointers-mips
            -DEXPECTED=P
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_integer_pointer_initializers_mips PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;middle;feature.initializer;feature.pointer;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    # -fwrapv at run time and in translation-time evaluation.
    foreach(level O0 O2 O3)
        add_test(NAME cross_runtime_wrapv_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/wrapv.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-wrapv-${level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level};-fwrapv"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_wrapv_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;feature.optimization;feature.evaluation;opt.${level}")
        add_test(NAME cross_runtime_wrapv_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/wrapv.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-wrapv-mips-${level}
                -DEXPECTED=P "-DCC_FLAGS=-${level};-fwrapv"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_wrapv_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.semantics;feature.optimization;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    # Subscript indices narrower and wider than an address.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_subscripts_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/subscripts.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-subscripts-${level}
                -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_subscripts_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;feature.array;feature.evaluation;opt.${level}")
        add_test(NAME cross_runtime_subscripts_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/subscripts.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-subscripts-mips-${level}
                -DEXPECTED=P -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_subscripts_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.semantics;feature.array;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(endian big little)
            add_test(NAME cross_runtime_subscripts_n64_${endian}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/subscripts.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-subscripts-n64-${endian}-${level}
                    -DN64_ENDIAN=${endian} -DEXTRA_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_runtime_subscripts_n64_${endian}_${level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.semantics;feature.array;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # Floating intrinsics at run time, in evaluation, and through LLVM IR.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_floating_intrinsics_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-${level}
                -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_intrinsics_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.intrinsics;feature.evaluation;opt.${level}")
        add_test(NAME cross_runtime_floating_intrinsics_avx_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-avx-${level}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level};-mavx"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_intrinsics_avx_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.intrinsics;isa.avx;opt.${level}")
        add_test(NAME cross_runtime_floating_intrinsics_llvm_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DCC_FLAGS=-${level}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-${level}.ll
                -DENTRY=test_entry -DEXPECTED=0
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
        set_tests_properties(cross_runtime_floating_intrinsics_llvm_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.llvm;feature.floating;feature.intrinsics;opt.${level}")
        add_test(NAME cross_runtime_floating_intrinsics_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-mips-${level}
                -DEXPECTED=P -DCC_FLAGS=-${level} "-DMIPS_CPUS=vr4300;r3000;r6000"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_floating_intrinsics_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.floating;feature.intrinsics;isa.vr4300;isa.r3000;isa.r6000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(endian big little)
            add_test(NAME cross_runtime_floating_intrinsics_n64_${endian}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-n64-${endian}-${level}
                    -DN64_ENDIAN=${endian} -DEXTRA_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_runtime_floating_intrinsics_n64_${endian}_${level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.floating;feature.intrinsics;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # The x87 f80 and binary128 forms.
    foreach(level O0 O1 O2)
        add_test(NAME cross_runtime_floating_intrinsics_x87_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics_x86.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-intrinsics-x87-${level}
                -DENTRY=test_entry -DEXPECTED=0 -DCC_FLAGS=-${level}
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_intrinsics_x87_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.intrinsics;feature.evaluation;opt.${level}")
    endforeach()
    # The MXCSR flags of operations under the trapping-fp and default profiles.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_floating_flags_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_environment/flags_runtime.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/floating_environment/environments.xm
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-flags-${level}
                -DLEVEL=${level}
                -P "${CMAKE_SOURCE_DIR}/test/middle/floating_environment/flags_runtime.cmake")
        set_tests_properties(cross_runtime_floating_flags_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.floating;feature.model;opt.${level}")
    endforeach()
    add_test(NAME cross_floating_intrinsic_errors
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics_errors.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-intrinsic-errors
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/floating_intrinsics_errors.cmake")
    set_tests_properties(cross_floating_intrinsic_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.floating;feature.intrinsics")
    # Bit intrinsics at run time and in evaluation: x86-64 with and without
    # LZCNT, TZCNT, and POPCNT, with no bit instructions at all, and through
    # LLVM IR; MIPS I, III, MIPS32, MIPS32 without CLZ, and n64.
    set(bit_intrinsics_source ${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics.x)
    set(bit_intrinsics_cpus ${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics_cpus.xm)
    foreach(level O0 O2)
        foreach(variant baseline features disabled none)
            if(variant STREQUAL features)
                set(bit_flags "-${level};-mlzcnt;-mbmi;-mpopcnt")
            elseif(variant STREQUAL disabled)
                set(bit_flags "-${level};-march=haswell;-mno-lzcnt;-mno-bmi;-mno-popcnt")
            elseif(variant STREQUAL none)
                set(bit_flags "-${level};--model=${bit_intrinsics_cpus};-march=x86-no-bit-instructions")
            else()
                set(bit_flags "-${level}")
            endif()
            add_test(NAME cross_runtime_bit_intrinsics_${variant}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${bit_intrinsics_source}
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bit-intrinsics-${variant}-${level}
                    -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${bit_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_bit_intrinsics_${variant}_${level} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;feature.intrinsics;feature.evaluation;opt.${level}")
        endforeach()
        if(CROSS_ENABLE_LLVM_TEXT)
            add_test(NAME cross_runtime_bit_intrinsics_llvm_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc> "-DCC_FLAGS=-${level};-mlzcnt;-mbmi;-mpopcnt"
                    -DSOURCE=${bit_intrinsics_source}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bit-intrinsics-${level}.ll
                    -DENTRY=test_entry -DEXPECTED=0
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
            set_tests_properties(cross_runtime_bit_intrinsics_llvm_${level} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.llvm;feature.intrinsics;opt.${level}")
        endif()
        add_test(NAME cross_runtime_bit_intrinsics_mips_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${bit_intrinsics_source}
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bit-intrinsics-mips-${level}
                -DEXPECTED=P -DCC_FLAGS=-${level} "-DMIPS_CPUS=vr4300;r3000"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_bit_intrinsics_mips_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.intrinsics;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        add_test(NAME cross_runtime_bit_intrinsics_mips32_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${bit_intrinsics_source}
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bit-intrinsics-mips32-${level}
                -DEXPECTED=P "-DCC_FLAGS=-${level};--model=${bit_intrinsics_cpus}"
                "-DMIPS_CPUS=mips32;mips32r2;mips32-noclz" -DQEMU_CPU=MIPS64R2-generic
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_bit_intrinsics_mips32_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.intrinsics;feature.model;isa.mips32;isa.mips32r2;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(endian big little)
            add_test(NAME cross_runtime_bit_intrinsics_n64_${endian}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${bit_intrinsics_source}
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_n64_entry_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bit-intrinsics-n64-${endian}-${level}
                    -DN64_ENDIAN=${endian} -DEXTRA_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_runtime_bit_intrinsics_n64_${endian}_${level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.intrinsics;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_bit_intrinsics_codegen
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DMODEL=${bit_intrinsics_cpus}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bit-intrinsics-codegen
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics_codegen.cmake")
    set_tests_properties(cross_bit_intrinsics_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.intrinsics;feature.model")
    add_test(NAME cross_bit_intrinsic_errors
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics_errors.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bit-intrinsic-errors
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/bit_intrinsics_errors.cmake")
    set_tests_properties(cross_bit_intrinsic_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.intrinsics")
    if(CROSS_ENABLE_GCC_GIMPLE_TEXT AND CROSS_TEST_GCC)
        add_test(NAME cross_gcc_gimple_wrapv
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DGCC=${CROSS_TEST_GCC}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/semantics/wrapv.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_wrapv_harness.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gimple-wrapv
                "-DLEVELS=O0;O2;O3" -DCC_FLAGS=-fwrapv -DEXPECTED=0
                "-DPATTERN== -cross_domain_left_"
                -P "${CMAKE_SOURCE_DIR}/test/backend/gcc/gimple_runtime.cmake")
        set_tests_properties(cross_gcc_gimple_wrapv PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.gcc;feature.semantics;opt.O0;opt.O2;opt.O3")
    endif()
    # -fbounds-trap instrumentation.
    add_test(NAME cross_runtime_bounds_trap
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/bounds_trap.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bounds-trap
            -P "${CMAKE_SOURCE_DIR}/test/middle/bounds_trap.cmake")
    set_tests_properties(cross_runtime_bounds_trap PROPERTIES
        LABELS "check.runtime;check.compile;arch.x86_64;arch.mips;backend.native;middle;feature.array;feature.array_bounds;opt.O0;opt.O2;opt.O3")
    foreach(level O0 O2)
        foreach(case bounds_ok bounds_past bounds_negative bounds_outer bounds_vla)
            if(case STREQUAL "bounds_ok")
                set(bounds_expected P)
            else()
                set(bounds_expected T)
            endif()
            add_test(NAME cross_runtime_bounds_trap_mips_${case}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/bounds_trap.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-bounds-trap-mips-${case}-${level}
                    -DEXPECTED=${bounds_expected}
                    "-DCC_FLAGS=-${level};-fbounds-trap;-DCASE=${case}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_runtime_bounds_trap_mips_${case}_${level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;middle;feature.array;feature.array_bounds;opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # Profile floating environments: speculation, evaluation, and traps.
    set(floating_environment ${CMAKE_SOURCE_DIR}/test/middle/floating_environment)
    add_test(NAME cross_floating_environment_speculation
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/speculation.x
            -DMODEL=${floating_environment}/environments.xm
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-environment-speculation
            -P "${floating_environment}/speculation.cmake")
    set_tests_properties(cross_floating_environment_speculation PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.floating;feature.optimization;feature.model;opt.O2;opt.O3")
    add_test(NAME cross_floating_environment_evaluation
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/evaluation.x
            -DMODEL=${floating_environment}/environments.xm
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-environment-evaluation
            -P "${floating_environment}/evaluation.cmake")
    set_tests_properties(cross_floating_environment_evaluation PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;frontend;feature.floating;feature.evaluation;feature.model;opt.O2")
    add_test(NAME cross_floating_nan_encoding
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/nan_encoding.x
            -DMODEL=${floating_environment}/environments.xm
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-nan-encoding
            -P "${floating_environment}/nan_encoding.cmake")
    set_tests_properties(cross_floating_nan_encoding PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.floating;feature.evaluation;feature.model;isa.vr4300;isa.mips32r2;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_floating_nan_x86_64_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${floating_environment}/nan_runtime.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-nan-x86-64-${level}
            -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level};-DWIDE"
            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_nan_x86_64_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;frontend;feature.floating;feature.evaluation;opt.${level}")
        add_test(NAME cross_runtime_floating_nan_propagation_x86_64_${level} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${floating_environment}/nan_propagation.x
            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-nan-propagation-x86-64-${level}
            -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=-${level}"
            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_floating_nan_propagation_x86_64_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;frontend;feature.floating;feature.evaluation;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_floating_nan_mips
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/nan_runtime.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-nan-mips
            -DEXPECTED=P -DMIPS_CPUS=vr4300
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
    set_tests_properties(cross_runtime_floating_nan_mips PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;frontend;feature.floating;feature.evaluation;isa.vr4300;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_runtime_floating_nan_propagation_mips_nan2008
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/nan_propagation_mips.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
            -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare.ld
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-nan-propagation-mips
            -P "${floating_environment}/nan_propagation_mips.cmake")
    set_tests_properties(cross_runtime_floating_nan_propagation_mips_nan2008 PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;frontend;middle;feature.floating;feature.evaluation;isa.mips32r2;endian.big;endian.little;opt.O2"
        SKIP_REGULAR_EXPRESSION "skipping MIPS")
    add_test(NAME cross_floating_environment_lowering
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${floating_environment}/lowering.x
            -DMODEL=${floating_environment}/environments.xm
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-environment-lowering
            -P "${floating_environment}/lowering.cmake")
    set_tests_properties(cross_floating_environment_lowering PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;backend.native;feature.floating;feature.model;isa.avx;isa.vr4300;isa.r6000;opt.O2")
    foreach(case guarded_O2 guarded_O3 unguarded signaling fmin_signaling
                 constant_comparison)
        set(environment_flags "--model=${floating_environment}/environments.xm;-mprofile=trapping-fp")
        if(case MATCHES "^guarded_(.*)")
            set(environment_expected P)
            set(environment_level ${CMAKE_MATCH_1})
            list(APPEND environment_flags -${environment_level})
        else()
            set(environment_expected T)
            set(environment_level O2)
            string(TOUPPER "${case}" environment_case)
            list(APPEND environment_flags -D${environment_case})
        endif()
        add_test(NAME cross_runtime_floating_environment_mips_${case}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${floating_environment}/trap_runtime.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-floating-environment-mips-${case}
                -DEXPECTED=${environment_expected} "-DCC_FLAGS=${environment_flags}"
                "-DMIPS_CPUS=vr4300;r3000;r6000"
                -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_floating_environment_mips_${case} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.floating;feature.optimization;feature.model;isa.vr4300;isa.r3000;isa.r6000;endian.big;endian.little;opt.${environment_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_x86_64_feature_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_feature_codegen.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-feature-codegen
                -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/x86_64_feature_codegen.cmake")
    set_tests_properties(cross_x86_64_feature_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_x86_64_floating_reinterpret_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/floating_reinterpret_allocation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-reinterpret-allocation
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/floating_reinterpret_allocation.cmake")
    set_tests_properties(cross_x86_64_floating_reinterpret_allocation PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.floating;feature.register_allocation")
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
    set_tests_properties(cross_x86_64_isa_selection PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_register_allocation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/register-allocation
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.cmake")
    set_tests_properties(cross_register_allocation PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.register_allocation")
    add_test(NAME cross_x86_64_block_layout
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/block_layout.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/block-layout
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/block_layout.cmake")
    set_tests_properties(cross_x86_64_block_layout PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_x86_64_if_conversion
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/if_conversion.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/if-conversion
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/if_conversion.cmake")
    set_tests_properties(cross_x86_64_if_conversion PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_x86_64_loop_unrolling
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/loop_unrolling.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/loop-unrolling
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/loop_unrolling.cmake")
    set_tests_properties(cross_x86_64_loop_unrolling PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization")
    add_test(NAME cross_x86_64_slp_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/slp_vectorization.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/slp-vectorization
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/slp_vectorization.cmake")
    set_tests_properties(cross_x86_64_slp_vectorization PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector")
    add_test(NAME cross_x86_64_red_zone
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/red_zone.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/red-zone
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/red_zone.cmake")
    set_tests_properties(cross_x86_64_red_zone PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_x86_64_stack_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stack-alignment
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/stack_alignment.cmake")
    set_tests_properties(cross_x86_64_stack_alignment PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_function_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/function_alignment.x
                -DERRORS=${CMAKE_SOURCE_DIR}/test/backend/native/function_alignment_errors.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/native/function_alignment_harness.cpp
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/function-alignment
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/function_alignment.cmake")
    set_tests_properties(cross_function_alignment PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.frame;feature.object")
    add_test(NAME cross_function_metadata
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/function_metadata.x
                -DERRORS=${CMAKE_SOURCE_DIR}/test/backend/native/function_metadata_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/function-metadata
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/function_metadata.cmake")
    set_tests_properties(cross_function_metadata PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.object")
    add_test(NAME cross_symbol_metadata
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DBINDING=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_binding.x
                -DHIDDEN=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_hidden.x
                -DVISIBILITY=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_visibility.x
                -DERRORS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_metadata_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/symbol-metadata
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/symbol_metadata.cmake")
    set_tests_properties(cross_symbol_metadata PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.object")
    add_test(NAME cross_zero_init_in_data
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/zero-init-in-data
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/zero_init_in_data.cmake")
    set_tests_properties(cross_zero_init_in_data PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.object")
    add_test(NAME cross_debug_info
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info.x
                -DHEADER=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_header.x
                -DPROBE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DFRAMES=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_frames.x
                -DINLINE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_inline.x
                -DENUMERATIONS=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_enum.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/debug-info
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/debug_info.cmake")
    set_tests_properties(cross_debug_info PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.debug;feature.object;feature.frame;feature.enum")
    add_test(NAME cross_debug_info_mips
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info.x
                -DHEADER=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_header.x
                -DFRAMES=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_frames.x
                -DINLINE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_inline.x
                -DENUMERATIONS=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_enum.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/debug-info-mips
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_mips.cmake")
    set_tests_properties(cross_debug_info_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.debug;feature.object;feature.frame;feature.enum;endian.big;endian.little")
    add_test(NAME cross_debug_info_noreturn
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_noreturn.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/debug-info-noreturn
                -DHOST_TARGET=${cross_default_target}
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_noreturn.cmake")
    set_tests_properties(cross_debug_info_noreturn PROPERTIES
        LABELS "check.compile;check.runtime;arch.x86_64;arch.mips;backend.native;feature.debug;feature.frame;opt.O0;opt.O2")
    foreach(gdb_config
            "n64_big;mips64-unknown-elf;n64;qemu-system-mips64;MIPS64R2-generic;mips64;elf64btsmip;bare-n64.ld;start64;big"
            "n64_little;mips64el-unknown-elf;n64;qemu-system-mips64el;MIPS64R2-generic;mips64;elf64ltsmip;bare-n64.ld;start64;little"
            "o32_big;mips-unknown-elf;o32;qemu-system-mips;24Kf;mips1;elf32btsmip;bare.ld;start32;big"
            "o32_little;mipsel-unknown-elf;o32;qemu-system-mipsel;24Kf;mips1;elf32ltsmip;bare.ld;start32;little")
        list(GET gdb_config 0 gdb_name)
        list(GET gdb_config 1 gdb_triple)
        list(GET gdb_config 2 gdb_abi)
        list(GET gdb_config 3 gdb_qemu)
        list(GET gdb_config 4 gdb_cpu)
        list(GET gdb_config 5 gdb_mc_cpu)
        list(GET gdb_config 6 gdb_emulation)
        list(GET gdb_config 7 gdb_linker)
        list(GET gdb_config 8 gdb_start)
        list(GET gdb_config 9 gdb_endian)
        add_test(NAME cross_debug_info_gdb_${gdb_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_gdb.x
                    -DSTART=${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_gdb_${gdb_start}.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/${gdb_linker}
                    -DTRIPLE=${gdb_triple}
                    -DABI=${gdb_abi}
                    -DQEMU_NAME=${gdb_qemu}
                    -DQEMU_CPU=${gdb_cpu}
                    -DMC_CPU=${gdb_mc_cpu}
                    -DEMULATION=${gdb_emulation}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/debug-info-gdb-${gdb_name}
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/debug_info_gdb.cmake")
        set_tests_properties(cross_debug_info_gdb_${gdb_name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.debug;feature.frame;endian.${gdb_endian}")
    endforeach()
    add_test(NAME cross_stack_usage
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/stack_usage.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stack-usage
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/stack_usage.cmake")
    set_tests_properties(cross_stack_usage PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.stack_usage;feature.frame;feature.driver;endian.big;endian.little")
    add_test(NAME cross_symbol_indirection
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DALIAS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_alias.x
                -DWEAK_ALIAS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_weak_alias.x
                -DWEAKREF=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_weakref.x
                -DWEAKREF_DECLS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_weakref_declarations.x
                -DERRORS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_indirection_errors.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/native/symbol_alias_harness.cpp
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/symbol-indirection
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/symbol_indirection.cmake")
    set_tests_properties(cross_symbol_indirection PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.object;feature.calls")
    foreach(arch IN ITEMS
            "x86_64;x86_64-unknown-linux-gnu,x86_64-w64-windows-gnu,x86_64-apple-darwin"
            "mips;mips-unknown-elf,mips64el-unknown-elf")
        list(GET arch 1 generic_instance_targets)
        list(GET arch 0 arch)
        string(REPLACE "," ";" generic_instance_targets "${generic_instance_targets}")
        add_test(NAME cross_generic_instance_objects_${arch}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DFIRST=${CMAKE_SOURCE_DIR}/test/backend/native/generic_instance_first.x
                    -DSECOND=${CMAKE_SOURCE_DIR}/test/backend/native/generic_instance_second.x
                    "-DTARGETS=${generic_instance_targets}"
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-instance-${arch}
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/generic_instance.cmake")
        set_tests_properties(cross_generic_instance_objects_${arch} PROPERTIES
            LABELS "check.compile;arch.${arch};backend.native;feature.object;feature.generic;feature.linkage")
    endforeach()
    foreach(triple IN ITEMS x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
                            x86_64-apple-darwin mips64-unknown-elf mips64el-unknown-elf)
        string(REGEX MATCH "^(x86_64|mips)" arch "${triple}")
        set(emission_order_labels
            "check.compile;arch.${arch};backend.native;feature.object;feature.linkage;feature.raw")
        if(triple MATCHES "^mips64el")
            string(APPEND emission_order_labels ";endian.little")
        elseif(triple MATCHES "^mips")
            string(APPEND emission_order_labels ";endian.big")
        else()
            string(APPEND emission_order_labels ";feature.optimization")
        endif()
        add_test(NAME cross_emission_order_${triple}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DTRIPLE=${triple}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/emission-order-${triple}
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/emission_order.cmake")
        set_tests_properties(cross_emission_order_${triple} PROPERTIES
            LABELS "${emission_order_labels}")
    endforeach()
    add_test(NAME cross_emission_order_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DTRIPLE=${cross_default_target}
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/emission-order-runtime
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/emission_order.cmake")
    set_tests_properties(cross_emission_order_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.object;feature.linkage;feature.raw;feature.optimization")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_generic_instance_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/generic_instance_first.x
                    -DEXTRA_SOURCES=${CMAKE_SOURCE_DIR}/test/backend/native/generic_instance_second.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-generic-instance-${level}
                    -DENTRY=generic_instance_entry
                    -DEXPECTED=11
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_generic_instance_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.object;feature.generic;feature.linkage;opt.${level}")
    endforeach()
    foreach(level O0 O2)
        add_test(NAME cross_runtime_alignment_expressions_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/alignment_expressions.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/alignment-expressions-${level}
                    -DENTRY=alignment_expression_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_alignment_expressions_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.${level}")
    endforeach()
    add_test(NAME cross_alignment_expressions_mips
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/alignment_expressions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/alignment-expressions-mips
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/alignment_expressions_mips.cmake")
    set_tests_properties(cross_alignment_expressions_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.frame")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_aligned_typedef_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-runtime-${level}
                    -DENTRY=aligned_typedef_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_aligned_typedef_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.layout;feature.frame;feature.records;feature.evaluation;opt.${level}")
        add_test(NAME cross_runtime_aligned_typedef_llvm_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DCC_FLAGS=-${level}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-llvm-${level}.ll
                    -DENTRY=aligned_typedef_entry
                    -DEXPECTED=1
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
        set_tests_properties(cross_runtime_aligned_typedef_llvm_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.llvm;feature.layout;feature.records;opt.${level}")
        # o32 on R3000 and VR4300 in both byte orders, then n64 in each.
        add_test(NAME cross_mips_aligned_typedef_o32_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-o32-${level}
                    -DEXPECTED=P "-DCC_FLAGS=-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_aligned_typedef_o32_${level} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.layout;feature.frame;feature.records;profile.o32;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        foreach(endian big little)
            add_test(NAME cross_mips_aligned_typedef_n64_${level}_${endian}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef.x
                        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/native/aligned_typedef_start.s
                        -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-n64-${level}-${endian}
                        -DN64_ENDIAN=${endian} "-DEXTRA_FLAGS=-${level}"
                        -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_mips_aligned_typedef_n64_${level}_${endian} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.layout;feature.frame;feature.records;profile.n64;opt.${level};endian.${endian}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    # Under-aligned typedefs, compound literals, and braced initializers: x86-64
    # natively and through LLVM IR, o32 on R3000 and VR4300 and n64, each in both
    # byte orders.
    foreach(case underaligned_typedef compound_literals braced_initializers)
        string(REPLACE "_" "-" case_path "${case}")
        if(case STREQUAL underaligned_typedef)
            set(case_labels "feature.layout;feature.records;feature.pointer;feature.evaluation")
        else()
            set(case_labels "feature.initializer;feature.records;feature.array;feature.evaluation")
        endif()
        foreach(level O0 O2)
            add_test(NAME cross_runtime_${case}_${level}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/${case}.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${case_path}-runtime-${level}
                        -DENTRY=${case}_entry
                        -DEXPECTED=1
                        -DCC_FLAGS=-${level}
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_${case}_${level} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;${case_labels};opt.${level}")
            add_test(NAME cross_runtime_${case}_llvm_${level}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DCC_FLAGS=-${level}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/${case}.x
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${case_path}-llvm-${level}.ll
                        -DENTRY=${case}_entry
                        -DEXPECTED=1
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
            set_tests_properties(cross_runtime_${case}_llvm_${level} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.llvm;${case_labels};opt.${level}")
            add_test(NAME cross_mips_${case}_o32_${level}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/${case}.x
                        -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/native/${case}_start.s
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${case_path}-o32-${level}
                        -DEXPECTED=P "-DCC_FLAGS=-${level}"
                        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
            set_tests_properties(cross_mips_${case}_o32_${level} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;${case_labels};profile.o32;opt.${level};endian.big;endian.little"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
            foreach(endian big little)
                add_test(NAME cross_mips_${case}_n64_${level}_${endian}
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/${case}.x
                            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/backend/native/${case}_start.s
                            -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${case_path}-n64-${level}-${endian}
                            -DN64_ENDIAN=${endian} "-DEXTRA_FLAGS=-${level}"
                            -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
                set_tests_properties(cross_mips_${case}_n64_${level}_${endian} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;${case_labels};profile.n64;opt.${level};endian.${endian}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()
    add_test(NAME cross_underaligned_typedef_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/underaligned-typedef-codegen
                -P "${CMAKE_SOURCE_DIR}/test/language/underaligned_typedef.cmake")
    set_tests_properties(cross_underaligned_typedef_codegen PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;backend.native;feature.layout;feature.pointer;endian.big;endian.little")
    add_test(NAME cross_x86_64_dynamic_call_alignment
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_call_alignment.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-call-alignment
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_call_alignment.cmake")
    set_tests_properties(cross_x86_64_dynamic_call_alignment PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_x86_64_static_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/static-stack-probe
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/static_stack_probe.cmake")
    set_tests_properties(cross_x86_64_static_stack_probe PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_x86_64_dynamic_stack_probe
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_stack_probe.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-stack-probe
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/dynamic_stack_probe.cmake")
    set_tests_properties(cross_x86_64_dynamic_stack_probe PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.frame")
    add_test(NAME cross_x86_64_overaligned_vla
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/overaligned-vla
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.cmake")
    set_tests_properties(cross_x86_64_overaligned_vla PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.array;feature.frame")
    add_test(NAME cross_f128_native
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/f128.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/f128-native
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/f128.cmake")
    set_tests_properties(cross_f128_native PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.floating")
    add_test(NAME cross_runtime_managed_mir
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-managed-mir.ll
                -DENTRY=managed_mir_entry
                -DEXPECTED=72
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_managed_mir PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;middle;feature.ir")
    add_test(NAME cross_runtime_managed_mir_o2
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCC_FLAGS=-O2
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/managed_mir.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-managed-mir-o2.ll
                -DENTRY=managed_mir_entry
                -DEXPECTED=72
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_managed_mir_o2 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;middle;feature.ir;opt.O2")
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
    set_tests_properties(cross_runtime_native_control_intrinsics PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.intrinsics")
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
    set_tests_properties(cross_runtime_native_raw_structured_cfg PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.raw;opt.O2")
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
    set_tests_properties(cross_runtime_native_register_allocation PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.O2")
    add_test(NAME cross_runtime_native_register_allocation_o3
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-register-allocation-o3
                -DENTRY=register_allocation_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O3
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_register_allocation_o3 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.O3")
    add_test(NAME cross_runtime_native_register_allocation_os
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_allocation.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-register-allocation-os
                -DENTRY=register_allocation_entry
                -DEXPECTED=1
                -DCC_FLAGS=-Os
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_register_allocation_os PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.Os")
    foreach(level O1 O2 O3 Os Oz)
        add_test(NAME cross_runtime_native_register_alias_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_alias_live.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-register-alias-${level}
                    -DENTRY=register_alias_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_register_alias_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_native_register_alias_no_ra
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/register_alias_live.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-register-alias-no-ra
                -DENTRY=register_alias_entry
                -DEXPECTED=1
                "-DCC_FLAGS=-O2;-fno-register-allocation"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_register_alias_no_ra PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.O2;opt.noalloc")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_custom_result_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/model/custom_result_runtime.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-custom-result-${level}
                    -DENTRY=custom_result_entry
                    -DEXPECTED=1
                    "-DCC_FLAGS=--model=${CMAKE_SOURCE_DIR}/test/model/custom.y;-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_custom_result_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;opt.${level};profile.custom")
        add_test(NAME cross_runtime_native_musttail_custom_result_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_custom_result.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-musttail-custom-result-${level}
                    -DENTRY=musttail_custom_result_entry
                    -DEXPECTED=1
                    "-DCC_FLAGS=--model=${CMAKE_SOURCE_DIR}/test/model/custom.y;-${level};-fno-optimize-sibling-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_musttail_custom_result_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.musttail;opt.${level};profile.custom")
        add_test(NAME cross_runtime_native_musttail_reuse_home_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_reuse_home.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-musttail-reuse-home-${level}
                    -DENTRY=musttail_reuse_home_entry
                    -DEXPECTED=1
                    "-DCC_FLAGS=-${level};-fno-optimize-sibling-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_musttail_reuse_home_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.musttail;opt.${level}")
        add_test(NAME cross_runtime_native_musttail_manual_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_manual.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-musttail-manual-${level}
                    -DENTRY=musttail_manual_entry
                    -DEXPECTED=6
                    "-DCC_FLAGS=-${level};-fno-optimize-sibling-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_musttail_manual_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.musttail;opt.${level}")
        add_test(NAME cross_runtime_native_musttail_outputs_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_outputs.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-musttail-outputs-${level}
                    -DENTRY=musttail_outputs_entry
                    -DEXPECTED=7
                    "-DCC_FLAGS=-${level};-fno-optimize-sibling-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_musttail_outputs_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.musttail;opt.${level}")
    endforeach()
    add_test(NAME cross_musttail_manual_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DERRORS=${CMAKE_SOURCE_DIR}/test/language/musttail_manual_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/musttail-manual-errors
                -P "${CMAKE_SOURCE_DIR}/test/language/musttail_manual.cmake")
    set_tests_properties(cross_musttail_manual_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi;feature.musttail")
    foreach(mode O0 O2 noalloc)
        foreach(profile native custom)
            set(symbol_address_flags
                "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y" -fno-eval-calls)
            if(mode STREQUAL noalloc)
                list(APPEND symbol_address_flags -O2 -fno-register-allocation)
            else()
                list(APPEND symbol_address_flags -${mode})
            endif()
            if(profile STREQUAL custom)
                list(APPEND symbol_address_flags -mabi=odd_abi)
            endif()
            add_test(NAME cross_runtime_native_symbol_address_preservation_${mode}_${profile}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/symbol_address_preservation.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/backend/abi/symbol_address_preservation_runner.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-symbol-address-preservation-${mode}-${profile}
                    -DENTRY=symbol_address_preservation -DEXPECTED=61
                    "-DCC_FLAGS=${symbol_address_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_native_symbol_address_preservation_${mode}_${profile}
                PROPERTIES LABELS "backend.native;check.runtime;feature.abi;feature.address;arch.x86_64;profile.${profile};opt.${mode}")
        endforeach()
    endforeach()
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
    set_tests_properties(cross_runtime_native_loop_rotation PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.O2")
    # Inner loops whose first test folds rotate; references that do not.
    foreach(rotation_mode O3 O2-no-rotate)
        string(REGEX MATCH "^O[0-9s]" rotation_level "${rotation_mode}")
        if(rotation_mode STREQUAL "O2-no-rotate")
            set(rotation_flags "-O2;-fno-tree-loop-rotate")
        else()
            set(rotation_flags "-${rotation_mode}")
        endif()
        add_test(NAME cross_runtime_native_loop_rotation_${rotation_mode}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_rotation.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-loop-rotation-${rotation_mode}
                    -DENTRY=loop_rotation_entry
                    -DEXPECTED=0
                    "-DCC_FLAGS=${rotation_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_loop_rotation_${rotation_mode}
            PROPERTIES LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.optimization;opt.${rotation_level}")
    endforeach()
    foreach(rotation_mode O2 O3)
        add_test(NAME cross_runtime_loop_rotation_mips_${rotation_mode}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/loop_rotation.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/support/mips_entry_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-loop-rotation-mips-${rotation_mode}
                    -DEXPECTED=P -DCC_FLAGS=-${rotation_mode}
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_runtime_loop_rotation_mips_${rotation_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.optimization;isa.vr4300;isa.r3000;endian.big;endian.little;opt.${rotation_mode}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
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
    set_tests_properties(cross_runtime_native_parallel_copy PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;opt.Oz")
    if(WIN32)
        set(scratch_host_abi ms_abi)
    else()
        set(scratch_host_abi sysv_abi)
    endif()
    foreach(level O0 O2)
        foreach(abi ms_abi x)
            set(scratch_flags -${level} -mabi=${abi} "-DHOST_ABI=\"${scratch_host_abi}\"")
            add_test(NAME cross_runtime_native_implicit_scratch_${level}_${abi}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/implicit_scratch.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-implicit-scratch-${level}-${abi}
                        -DENTRY=implicit_scratch_entry
                        -DEXPECTED=63
                        "-DCC_FLAGS=${scratch_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_native_implicit_scratch_${level}_${abi} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;feature.abi;opt.${level}")
            add_test(NAME cross_runtime_native_argument_cycle_${level}_${abi}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/argument_cycle.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-argument-cycle-${level}-${abi}
                        -DENTRY=argument_cycle_entry
                        -DEXPECTED=4
                        "-DCC_FLAGS=${scratch_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_native_argument_cycle_${level}_${abi} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;feature.register_allocation;feature.abi;opt.${level}")
        endforeach()
        set(narrow_model "--model=${CMAKE_SOURCE_DIR}/test/model/narrow32.y")
        foreach(abi narrow32 narrow32_partial)
            set(narrow_flags -${level} ${narrow_model} -mabi=${abi} "-DHOST_ABI=\"${scratch_host_abi}\"")
            add_test(NAME cross_runtime_native_narrow_bank_${level}_${abi}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/narrow_bank.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-narrow-bank-${level}-${abi}
                        -DENTRY=narrow_entry
                        -DEXPECTED=127
                        "-DCC_FLAGS=${narrow_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_native_narrow_bank_${level}_${abi} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;opt.${level};profile.custom")
        endforeach()
        set(narrow_flags -${level} ${narrow_model} "-DHOST_ABI=\"${scratch_host_abi}\"")
        add_test(NAME cross_runtime_native_narrow_bank_preserved_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/narrow_bank_preserved.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-narrow-bank-preserved-${level}
                    -DENTRY=narrow_preserve_entry
                    -DEXPECTED=3
                    "-DCC_FLAGS=${narrow_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_narrow_bank_preserved_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.register_allocation;opt.${level};profile.custom")
    endforeach()
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
    set_tests_properties(cross_runtime_native_if_conversion PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;opt.O2")
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
    set_tests_properties(cross_runtime_native_loop_unrolling PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;opt.O3")
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
    set_tests_properties(cross_runtime_native_slp_vectorization PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;opt.O2")
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
    set_tests_properties(cross_runtime_native_stack_alignment PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.O2")
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
    set_tests_properties(cross_runtime_native_dynamic_call_alignment PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.O2")
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
    set_tests_properties(cross_runtime_native_static_stack_probe PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.O2")
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
    set_tests_properties(cross_runtime_native_dynamic_stack_probe PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.frame;opt.O2")
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
    set_tests_properties(cross_runtime_native_overaligned_vla PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.array;feature.frame;opt.O2")
    # Allocation is off at these levels; the frame still needs its CFA anchor.
    foreach(level O0 O1)
        string(TOLOWER ${level} level_name)
        add_test(NAME cross_runtime_native_overaligned_vla_${level_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/machine/overaligned_vla.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-overaligned-vla-${level_name}
                    -DENTRY=overaligned_vla_entry
                    -DEXPECTED=46
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_overaligned_vla_${level_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.array;feature.frame;opt.${level}")
    endforeach()
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
    set_tests_properties(cross_runtime_native_f128 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;opt.O2")
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
    set_tests_properties(cross_runtime_native_f128_differential PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.optimization;opt.O2")
    add_test(NAME cross_mir_verifier COMMAND $<TARGET_FILE:cross_mir_verifier_test>)
    set_tests_properties(cross_mir_verifier PROPERTIES
        LABELS "check.unit;arch.independent;feature.ir")
    add_test(NAME cross_abi_rules
        COMMAND $<TARGET_FILE:cross_abi_rules_test>
                "${CMAKE_SOURCE_DIR}/test/model/abi_dsl.y")
    set_tests_properties(cross_abi_rules PROPERTIES
        LABELS "check.unit;arch.independent;feature.abi")
    # How aapcs64 and the AArch64 Cross ABI place example signatures.
    add_test(NAME cross_aarch64_abi_layouts
        COMMAND $<TARGET_FILE:cross_abi_layouts_test> aarch64
                "${CMAKE_SOURCE_DIR}/test/target/aarch64/abi_layouts.txt")
    set_tests_properties(cross_aarch64_abi_layouts PROPERTIES
        LABELS "check.unit;arch.aarch64;feature.abi;feature.model")
    # uniform_members, max_members, the exhaust failure, and preserved views
    # in a user x86-64 model.
    add_test(NAME cross_model_homogeneous_abi
        COMMAND $<TARGET_FILE:cross_abi_layouts_test> custom
                "${CMAKE_SOURCE_DIR}/test/model/homogeneous.xm"
                "${CMAKE_SOURCE_DIR}/test/model/homogeneous.txt")
    set_tests_properties(cross_model_homogeneous_abi PROPERTIES
        LABELS "check.unit;arch.x86_64;feature.abi;feature.model")
    add_test(NAME cross_manual_abi_plan
        COMMAND $<TARGET_FILE:cross_manual_abi_plan_test>)
    set_tests_properties(cross_manual_abi_plan PROPERTIES
        LABELS "check.unit;arch.independent;feature.abi")
    add_test(NAME cross_x86_64_aggregate_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport_runner.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-transport
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport.cmake")
    set_tests_properties(cross_x86_64_aggregate_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.records")
    add_test(NAME cross_x86_64_aligned_typedef_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/aligned_typedef.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/backend/abi/aligned_typedef_runner.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aligned-typedef-abi
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/aggregate_transport.cmake")
    set_tests_properties(cross_x86_64_aligned_typedef_abi PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.records;feature.calls")
    add_test(NAME cross_dynamic_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/dynamic_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-abi
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/dynamic_abi.cmake")
    set_tests_properties(cross_dynamic_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi")
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
    set_tests_properties(cross_runtime_dynamic_abi_clobbers PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;opt.O3")
    add_test(NAME cross_native_backend
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.x
                -DHARNESS=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend_runtime.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-backend
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.cmake")
    set_tests_properties(cross_native_backend PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native")
    add_test(NAME cross_macho_object
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DNATIVE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/native_backend.x
                -DRAW_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/raw/raw_naked.x
                -DDATA_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/data/static_objects.x
                -DNAMESPACE_SOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DSYMBOL_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/macho_symbols.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/macho-object
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/macho_object.cmake")
    set_tests_properties(cross_macho_object PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.object")
    add_test(NAME cross_vectors
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/vector/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/vectors.s")
    set_tests_properties(cross_vectors PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;opt.O2")
    add_test(NAME cross_runtime_vectors_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vectors.ll
                -DENTRY=vector_entry
                -DEXPECTED=47
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_vectors_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.vector;opt.O2")
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
    set_tests_properties(cross_runtime_native_vectors PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;opt.O2")
    # Vectors of constant lanes, which load as one literal.
    foreach(constant_mode O0 O2 O2-avx2 O2-medium O3)
        string(REGEX MATCH "^O[0-9s]" constant_level "${constant_mode}")
        set(constant_flags "-${constant_level}")
        if(constant_mode MATCHES "-avx2$")
            list(APPEND constant_flags "-march=x86-64-v3")
        elseif(constant_mode MATCHES "-medium$")
            list(APPEND constant_flags "-mcmodel=medium")
        endif()
        add_test(NAME cross_runtime_vector_constant_lanes_${constant_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/constant_lanes.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-vector-constant-lanes-${constant_mode}
                -DENTRY=test_entry -DEXPECTED=0
                "-DCC_FLAGS=${constant_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_vector_constant_lanes_${constant_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;opt.${constant_level}")
    endforeach()
    # Values in the result register across vector staging through XMM0-XMM2.
    foreach(scratch_mode O0 O2 O3 O2-avx2 O3-avx2 O2-fma)
        string(REGEX MATCH "^O[0-9s]" scratch_level "${scratch_mode}")
        set(scratch_flags "-${scratch_level};-fno-eval-calls")
        set(scratch_labels "")
        if(scratch_mode MATCHES "-(avx2|fma)$")
            list(APPEND scratch_flags "-march=x86-64-v3")
            set(scratch_labels ";isa.avx")
        endif()
        if(scratch_mode MATCHES "-fma$")
            list(APPEND scratch_flags "-ffp-contract=fast")
        endif()
        add_test(NAME cross_runtime_vector_scratch_endpoints_${scratch_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/scratch_endpoints.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-vector-scratch-endpoints-${scratch_mode}
                -DENTRY=test_entry -DEXPECTED=0 "-DCC_FLAGS=${scratch_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_vector_scratch_endpoints_${scratch_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;feature.register_allocation${scratch_labels};opt.${scratch_level}")
    endforeach()
    add_test(NAME cross_vector_constant_lanes_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/constant_lanes.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-constant-lanes
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/constant_lanes.cmake")
    set_tests_properties(cross_vector_constant_lanes_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;opt.O2")
    # Splats of loads, stores of lanes, and partial SLP groups.
    foreach(lane_mode O0 O2 O2-avx2 O2-avx2-unfolded O3-avx2 Os)
        string(REGEX MATCH "^O[0-9s]" lane_level "${lane_mode}")
        set(lane_flags "-${lane_level}")
        if(lane_mode MATCHES "-avx2")
            list(APPEND lane_flags "-march=x86-64-v3")
        endif()
        if(lane_mode MATCHES "-unfolded$")
            list(APPEND lane_flags "-fno-fold-memory-operands")
        endif()
        add_test(NAME cross_runtime_vector_lane_memory_${lane_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/lane_memory.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-vector-lane-memory-${lane_mode}
                -DENTRY=test_entry -DEXPECTED=0
                "-DCC_FLAGS=${lane_flags};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_vector_lane_memory_${lane_mode} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;opt.${lane_level}")
    endforeach()
    add_test(NAME cross_vector_lane_memory_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/lane_memory.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-lane-memory
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/lane_memory.cmake")
    set_tests_properties(cross_vector_lane_memory_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;opt.O2")
    add_test(NAME cross_vector_loop_optimization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/loop_optimization.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-loop-optimization.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/loop_optimization.cmake")
    set_tests_properties(cross_vector_loop_optimization PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;feature.optimization")
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
    set_tests_properties(cross_runtime_native_vector_loop_optimization PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;feature.optimization;opt.O2")
    foreach(variant IN ITEMS "default;-O3" "sse41;-O2;-march=x86-64-v2")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_native_vector_reduction_bounds_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/reduction_bounds.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vector-reduction-bounds-${variant_name}
                    -DENTRY=vector_reduction_bounds_entry
                    -DEXPECTED=6
                    "-DCC_FLAGS=${variant}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_vector_reduction_bounds_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector")
    endforeach()
    foreach(variant IN ITEMS "default;-O3" "sse41;-O2;-march=x86-64-v2"
                             "avx2;-O3;-march=x86-64-v3")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_native_vector_memory_alignment_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/memory_alignment.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vector-memory-alignment-${variant_name}
                    -DENTRY=vector_memory_alignment_entry
                    -DEXPECTED=8
                    "-DCC_FLAGS=${variant}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_vector_memory_alignment_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;feature.optimization")
    endforeach()
    # Record members at power-of-two strides an index scale cannot encode.
    add_test(NAME cross_x86_64_record_strides
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/record_strides.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-record-strides
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/record_strides.cmake")
    set_tests_properties(cross_x86_64_record_strides PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization;opt.O2")
    foreach(variant IN ITEMS "O0;-O0" "O2;-O2" "O3;-O3" "Os;-Os"
                             "uncombined;-O2;-fno-combine-addresses")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_x86_64_record_strides_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/record_strides.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-x86-64-record-strides-${variant_name}
                    -DENTRY=test_entry -DEXPECTED=0
                    "-DCC_FLAGS=${variant};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        list(GET variant 0 stride_level)
        string(SUBSTRING "${stride_level}" 1 -1 stride_level)
        set_tests_properties(cross_runtime_x86_64_record_strides_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;opt.${stride_level}")
    endforeach()
    # Selects of a decrement by its borrow and masked zero tests.
    add_test(NAME cross_x86_64_borrow_selects
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/borrow_selects.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-borrow-selects
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/borrow_selects.cmake")
    set_tests_properties(cross_x86_64_borrow_selects PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization;opt.O2")
    foreach(variant IN ITEMS "O0;-O0" "O2;-O2" "O3;-O3" "Os;-Os"
                             "unfused;-O2;-fno-compare-select-fusion")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_x86_64_borrow_selects_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/borrow_selects.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-x86-64-borrow-selects-${variant_name}
                    -DENTRY=test_entry -DEXPECTED=0
                    "-DCC_FLAGS=${variant};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        list(GET variant 0 borrow_level)
        string(SUBSTRING "${borrow_level}" 1 -1 borrow_level)
        set_tests_properties(cross_runtime_x86_64_borrow_selects_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization;opt.${borrow_level}")
    endforeach()
    # Pointer reads at a constant displacement as ALU and SSE memory operands.
    add_test(NAME cross_x86_64_memory_operands
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/memory_operands.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-64-memory-operands
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/memory_operands.cmake")
    set_tests_properties(cross_x86_64_memory_operands PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.optimization;opt.O2")
    foreach(variant IN ITEMS "O2;-O2" "avx2;-O2;-march=x86-64-v3"
                             "unfolded;-O2;-fno-fold-memory-operands" "O0;-O0")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_native_memory_operands_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/memory_operands.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-memory-operands-${variant_name}
                    -DENTRY=memory_operands_entry
                    -DEXPECTED=0
                    "-DCC_FLAGS=${variant};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_memory_operands_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.optimization")
    endforeach()
    # The host cannot run AVX-512; its 512-bit sign selects must still compile.
    add_test(NAME cross_x86_64_vector_select_avx512
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -target x86_64-unknown-linux-gnu
                -march=x86-64-v4
                "${CMAKE_SOURCE_DIR}/test/backend/native/vector/qword_multiply.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/vector-select-avx512.s")
    set_tests_properties(cross_x86_64_vector_select_avx512 PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;isa.avx512")
    # Ordered floating comparisons that decide a branch, NaN operands included.
    foreach(variant IN ITEMS "O2;-O2" "avx2;-O2;-march=x86-64-v3"
                             "unfused;-O2;-fno-compare-branch-fusion" "O0;-O0")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_native_float_branches_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/float_branches.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-float-branches-${variant_name}
                    -DENTRY=float_branches_entry
                    -DEXPECTED=0
                    "-DCC_FLAGS=${variant};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_float_branches_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.optimization")
    endforeach()
    # A vectorized u64 multiply by a constant, with and without packed qwords.
    foreach(variant IN ITEMS "default;-O2" "sse41;-O2;-march=x86-64-v2"
                             "avx2;-O3;-march=x86-64-v3")
        list(GET variant 0 variant_name)
        list(REMOVE_AT variant 0)
        add_test(NAME cross_runtime_native_vector_qword_multiply_${variant_name}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/qword_multiply.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vector-qword-multiply-${variant_name}
                    -DENTRY=qword_multiply_entry
                    -DEXPECTED=0
                    "-DCC_FLAGS=${variant}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_vector_qword_multiply_${variant_name} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;feature.optimization")
    endforeach()
    add_test(NAME cross_x86_64_early_exit_vectorization
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/early_exit.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/early-exit-vectorization.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/early_exit.cmake")
    set_tests_properties(cross_x86_64_early_exit_vectorization PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;feature.optimization")
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
    set_tests_properties(cross_runtime_x86_64_early_exit_vectorization PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;feature.optimization;opt.O3")
    add_test(NAME cross_wide_vectors
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/wide-vectors.s")
    set_tests_properties(cross_wide_vectors PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector;opt.O2")
    add_test(NAME cross_runtime_wide_vectors_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wide-vectors.ll
                -DENTRY=wide_vector_entry
                -DEXPECTED=76
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_wide_vectors_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.vector;opt.O2")
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
    set_tests_properties(cross_runtime_native_wide_vectors PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.vector;opt.O2")
    add_test(NAME cross_registered_wide_vector_features
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/wide_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/registered-wide-vector
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/automatic_wide_vector_features.cmake")
    set_tests_properties(cross_registered_wide_vector_features PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.vector")
    add_test(NAME cross_x86_64_vector_extensions
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/vector/extensions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/x86-vector-extensions
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/extensions.cmake")
    set_tests_properties(cross_x86_64_vector_extensions PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.vector")
    add_test(NAME cross_vector_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/vector
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/vector/diagnostics.cmake")
    set_tests_properties(cross_vector_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.vector")
    add_test(NAME cross_vector_constant_indices
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/vector_index_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vector-index-errors
                -P "${CMAKE_SOURCE_DIR}/test/frontend/vector_index_checks.cmake")
    set_tests_properties(cross_vector_constant_indices PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.vector")
    foreach(helper_mode native custom mips mipsel mips64 mips64el)
        add_test(NAME cross_source_constant_helpers_${helper_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/source-constant-helpers/${helper_mode}
                -DMODE=${helper_mode}
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -P "${CMAKE_SOURCE_DIR}/test/frontend/source_constant_helpers.cmake")
        if(helper_mode MATCHES "^mips")
            set(helper_arch mips)
        else()
            set(helper_arch x86_64)
        endif()
        set_tests_properties(cross_source_constant_helpers_${helper_mode} PROPERTIES
            LABELS "frontend;feature.evaluation;feature.source_constraints;feature.pointer;feature.vector;arch.${helper_arch};profile.${helper_mode};diagnostics")
        add_test(NAME cross_evaluation_resources_${helper_mode}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evaluation-resources/${helper_mode}
                -DMODE=${helper_mode}
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -P "${CMAKE_SOURCE_DIR}/test/frontend/evaluation_resources.cmake")
        set_tests_properties(cross_evaluation_resources_${helper_mode} PROPERTIES
            LABELS "frontend;feature.evaluation;feature.resources;feature.array;arch.${helper_arch};profile.${helper_mode};diagnostics")
        if(helper_mode MATCHES "^mips.*el$")
            set_property(TEST cross_evaluation_resources_${helper_mode} APPEND PROPERTY LABELS endian.little)
        elseif(helper_mode MATCHES "^mips")
            set_property(TEST cross_evaluation_resources_${helper_mode} APPEND PROPERTY LABELS endian.big)
        else()
            set_property(TEST cross_evaluation_resources_${helper_mode} APPEND PROPERTY LABELS "endian.little;check.runtime")
        endif()
    endforeach()
    add_test(NAME cross_array_bound_checks
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/array_bound_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/array-bound-errors
                -P "${CMAKE_SOURCE_DIR}/test/frontend/array_bounds_checks.cmake")
    set_tests_properties(cross_array_bound_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.array")
    add_test(NAME cross_numeric_conversions
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/conversion/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/numeric-conversions.s")
    set_tests_properties(cross_numeric_conversions PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.floating;opt.O2")
    add_test(NAME cross_runtime_numeric_conversions_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/conversion/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/numeric-conversions.ll
                -DENTRY=conversion_entry
                -DEXPECTED=41
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_numeric_conversions_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.floating;opt.O2")
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
    set_tests_properties(cross_runtime_native_numeric_conversions PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_int128_f80_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/conversion/int128_f80.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-int128-f80-${level}
                    -DENTRY=int128_f80_entry
                    -DEXPECTED=16
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_int128_f80_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;opt.${level}")
        add_test(NAME cross_runtime_native_int128_float_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/conversion/int128_float.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-int128-float-${level}
                    -DENTRY=int128_float_entry
                    -DEXPECTED=180
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_int128_float_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.floating;feature.integer128;opt.${level}")
    endforeach()
    add_test(NAME cross_arrays
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/array/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/arrays.s")
    set_tests_properties(cross_arrays PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.array;opt.O2")
    add_test(NAME cross_runtime_arrays_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/arrays.ll
                -DENTRY=array_entry
                -DEXPECTED=44
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_arrays_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.array;opt.O2")
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
    set_tests_properties(cross_runtime_native_arrays PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.array;opt.O2")
    add_test(NAME cross_vla
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/vla.s")
    set_tests_properties(cross_vla PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.array;opt.O2")
    add_test(NAME cross_runtime_vla_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vla.ll
                -DENTRY=vla_entry
                -DEXPECTED=100
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_vla_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.array;opt.O2")
    add_test(NAME cross_runtime_native_vla
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vla
                -DENTRY=vla_entry
                -DEXPECTED=100
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_vla PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.array;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_vla_empty_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_empty_runtime.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vla-empty-${level}
                    -DENTRY=vla_empty_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_vla_empty_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.array;opt.${level}")
        add_test(NAME cross_runtime_native_vla_nonempty_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_nonempty_initializer.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vla-nonempty-${level}
                    -DENTRY=vla_nonempty_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_vla_nonempty_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.array;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_vla_nonempty_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_nonempty_initializer.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vla-nonempty.ll
                -DENTRY=vla_nonempty_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_vla_nonempty_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.array;opt.O2")
    add_test(NAME cross_runtime_vla_nonempty_bounds
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_nonempty_bounds.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-vla-nonempty-bounds
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/array/vla_nonempty_bounds.cmake")
    set_tests_properties(cross_runtime_vla_nonempty_bounds PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.array")
    add_test(NAME cross_array_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/array
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/array-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/array/diagnostics.cmake")
    set_tests_properties(cross_array_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;backend.native;feature.array")
    add_test(NAME cross_aggregates
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/aggregates.s")
    set_tests_properties(cross_aggregates PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.records;opt.O2")
    add_test(NAME cross_runtime_aggregates_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregates.ll
                -DENTRY=aggregate_entry
                -DEXPECTED=64
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_aggregates_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.records;opt.O2")
    add_test(NAME cross_runtime_native_aggregates
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-aggregates
                -DENTRY=aggregate_entry
                -DEXPECTED=64
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_aggregates PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.records;opt.O2")
    add_test(NAME cross_bitfields
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bit_field.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/bitfields.s")
    set_tests_properties(cross_bitfields PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.records;opt.O2")
    add_test(NAME cross_bitfields_gimple_debug
        COMMAND $<TARGET_FILE:cross_cc> -emit-gimple -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bit_field.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/bitfields.gimple.c")
    set_tests_properties(cross_bitfields_gimple_debug PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.gcc;feature.records;opt.O2")
    add_test(NAME cross_runtime_bitfields_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bitfield_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bitfields.ll
                -DENTRY=bitfield_entry
                -DEXPECTED=36
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_bitfields_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.records;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_bitfields_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bitfield_runtime.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-bitfields-${level}
                    -DENTRY=bitfield_entry
                    -DEXPECTED=36
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_bitfields_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.records;opt.${level}")
    endforeach()
    add_test(NAME cross_bitfield_targets
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bitfield_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bitfield-targets
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/bitfield_targets.cmake")
    set_tests_properties(cross_bitfield_targets PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.records")
    add_test(NAME cross_aggregate_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/diagnostics.cmake")
    set_tests_properties(cross_aggregate_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.records")
    add_test(NAME cross_aggregate_initializer_targets
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-initializer-targets
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/aggregate/initializer_targets.cmake")
    set_tests_properties(cross_aggregate_initializer_targets PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.records")
    add_test(NAME cross_atomics
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/atomic/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/atomics.s")
    set_tests_properties(cross_atomics PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.atomics;opt.O2")
    add_test(NAME cross_runtime_atomics_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/atomic/runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomics.ll
                -DENTRY=atomic_entry
                -DEXPECTED=35
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_atomics_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.atomics;opt.O2")
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
    set_tests_properties(cross_runtime_native_atomics PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.atomics;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_atomic_compound_threads_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/atomic/compound_threads.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/backend/native/atomic/compound_threads.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomic-compound-threads-${level}
                -DENTRY=compound_counter_next -DEXPECTED=1
                "-DCC_FLAGS=-${level};-fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_atomic_compound_threads_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.atomics;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_compound_updates_llvm_debug
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/compound_updates.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/compound-updates.ll
            -DENTRY=syntax_raw_entry -DEXPECTED=61
            "-DCC_FLAGS=-O2;-fno-eval-calls;-DTEST_NARROW_ATOMICS"
            -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_compound_updates_llvm_debug PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;frontend;feature.compound_updates;opt.O2")
    add_test(NAME cross_atomic_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/atomic
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/atomic-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/atomic/diagnostics.cmake")
    set_tests_properties(cross_atomic_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.atomics")
    add_test(NAME cross_variadics
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/variadics.s")
    set_tests_properties(cross_variadics PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.variadic;opt.O2")
    add_test(NAME cross_runtime_native_variadics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-variadics
                -DENTRY=variadic_entry
                -DEXPECTED=10
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_variadics PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.variadic;opt.O2")
    add_test(NAME cross_variadic_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/backend/native/variadic
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadic-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/diagnostics.cmake")
    set_tests_properties(cross_variadic_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.variadic")
    if(CROSS_ENABLE_LLVM_TEXT)
        add_test(NAME cross_variadics_llvm_debug
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DLLVM_AS=llvm-as
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/runtime.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadics-debug
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/llvm.cmake")
        set_tests_properties(cross_variadics_llvm_debug PROPERTIES
            LABELS "check.compile;arch.x86_64;backend.llvm;feature.variadic")
    endif()
    # A user ABI's record_address state: its record's layout and alignment.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_variadic_record_state_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/record_state.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadic-record-state-${level}
                    -DENTRY=record_entry -DEXPECTED=0
                    "-DCC_FLAGS=-${level};--model=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/record_state.xm"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_variadic_record_state_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.variadic;feature.model;opt.${level}")
    endforeach()
    add_test(NAME cross_init_tables_objects
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/init_tables/objects.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/init-tables-objects
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/init_tables/objects.cmake")
    set_tests_properties(cross_init_tables_objects PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;backend.native;feature.init_tables;feature.object;feature.abi;endian.big;endian.little;profile.o32;profile.n64")
    add_test(NAME cross_init_tables_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/init-tables-diagnostics
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/init_tables/errors.cmake")
    set_tests_properties(cross_init_tables_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;feature.init_tables")
    # Programs linked with the system C runtime (GNU ld and glibc's crt),
    # also from the LLVM serializer's output when Clang compiles it.
    find_program(CROSS_TEST_GCC NAMES gcc)
    if(CROSS_TEST_GCC AND cross_default_target MATCHES "^x86_64-.*-linux")
        set(c_runtime_labels "check.runtime;arch.x86_64;backend.native;feature.interop;opt.O0;opt.O2")
        set(c_runtime_clang "")
        if(CROSS_ENABLE_LLVM_TEXT)
            find_program(CROSS_TEST_CLANG NAMES clang)
            if(CROSS_TEST_CLANG)
                set(c_runtime_clang -DCLANG=${CROSS_TEST_CLANG})
                string(APPEND c_runtime_labels ";backend.llvm")
            endif()
        endif()
        add_test(NAME cross_init_tables_runtime
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DGCC=${CROSS_TEST_GCC} ${c_runtime_clang}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/init_tables/tables.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/init-tables-runtime
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/init_tables/runtime.cmake")
        set_tests_properties(cross_init_tables_runtime PROPERTIES
            LABELS "${c_runtime_labels};feature.init_tables;feature.object")
        add_test(NAME cross_runtime_variadic_va_list
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DGCC=${CROSS_TEST_GCC} ${c_runtime_clang}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/va_list.x
                    -DDRIVER=${CMAKE_SOURCE_DIR}/test/backend/native/variadic/va_list_driver.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/variadic-va-list
                    -P "${CMAKE_SOURCE_DIR}/test/backend/native/variadic/va_list.cmake")
        set_tests_properties(cross_runtime_variadic_va_list PROPERTIES
            LABELS "${c_runtime_labels};feature.variadic;feature.abi")
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
    set_tests_properties(cross_runtime_native_automatic_call_boundary PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_loops
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/loops.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/loops.s")
    set_tests_properties(cross_loops PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.control;opt.O2")
    add_test(NAME cross_compile_time
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/compile_time.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/compile_time.s")
    set_tests_properties(cross_compile_time PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.evaluation;opt.O2")
    add_test(NAME cross_pointers
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/pointers.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/pointers.s")
    set_tests_properties(cross_pointers PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.pointer;opt.O2")
    add_test(NAME cross_expression_core_x86_64
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/expression_core.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/expression-core-x86_64.s")
    set_tests_properties(cross_expression_core_x86_64 PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.semantics;opt.O2")
    add_test(NAME cross_expression_core_x86_64_O0
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/language/expression_core.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/expression-core-x86_64-O0.s")
    set_tests_properties(cross_expression_core_x86_64_O0 PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.semantics;opt.O0")
    add_test(NAME cross_expression_core_mips64
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mprofile=mips64-n64
                "${CMAKE_SOURCE_DIR}/test/language/expression_core.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/expression-core-mips64.s")
    set_tests_properties(cross_expression_core_mips64 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.semantics;opt.O2")
    add_test(NAME cross_expression_core_mips64_O0
        COMMAND $<TARGET_FILE:cross_cc> -S -O0 -mprofile=mips64-n64
                "${CMAKE_SOURCE_DIR}/test/language/expression_core.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/expression-core-mips64-O0.s")
    set_tests_properties(cross_expression_core_mips64_O0 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.semantics;opt.O0")
    add_test(NAME cross_expression_core_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/expression-core-error
                -P "${CMAKE_SOURCE_DIR}/test/language/expression_core_errors.cmake")
    set_tests_properties(cross_expression_core_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;feature.semantics")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_embedded_members_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/embedded_members.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded-members-${level}
                    -DENTRY=embedded_members_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_embedded_members_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.x86_64;backend.native;feature.records;feature.evaluation;opt.${level}")
        add_test(NAME cross_mips_embedded_members_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/embedded_members.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/embedded_members_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded-members-mips-${level}
                    -DEXPECTED=P -DMIPS_CPUS=vr4300 -DMIPS_ENDIANS=b "-DCC_FLAGS=-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_embedded_members_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.mips;backend.native;feature.records;feature.evaluation;profile.o32;isa.vr4300;endian.big;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_embedded_members_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded-members-errors
                -P "${CMAKE_SOURCE_DIR}/test/language/embedded_members_errors.cmake")
    set_tests_properties(cross_embedded_members_errors PROPERTIES
        LABELS "check.compile;diagnostics;frontend;arch.x86_64;feature.records")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_type_reflection_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/type_reflection.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/type-reflection-${level}
                    -DENTRY=type_reflection_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_type_reflection_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.x86_64;backend.native;feature.meta;feature.records;opt.${level}")
        # Bit-field positions are reflected in each byte order.
        add_test(NAME cross_mips_type_reflection_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/type_reflection.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/type_reflection_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/type-reflection-mips-${level}
                    -DEXPECTED=P -DMIPS_CPUS=vr4300 "-DMIPS_ENDIANS=b;l" "-DCC_FLAGS=-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_type_reflection_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.mips;backend.native;feature.meta;feature.records;profile.o32;isa.vr4300;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_type_reflection_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/type-reflection-errors
                -P "${CMAKE_SOURCE_DIR}/test/frontend/type_reflection_errors.cmake")
    set_tests_properties(cross_type_reflection_errors PROPERTIES
        LABELS "check.compile;diagnostics;frontend;arch.x86_64;feature.meta")
    # Generic records and unions: instances, layout, deduction, local generic
    # records, embedding, and reflection, at translation time and at run time.
    foreach(level O0 O2)
        add_test(NAME cross_runtime_generic_records_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_records.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-records-${level}
                    -DENTRY=generic_records_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_records_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.x86_64;backend.native;feature.generic;feature.records;opt.${level}")
        # Pointer width and bit-field order differ from x86-64.
        add_test(NAME cross_mips_generic_records_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_records.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/generic_records_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-records-mips-${level}
                    -DEXPECTED=P -DMIPS_CPUS=vr4300 "-DMIPS_ENDIANS=b;l" "-DCC_FLAGS=-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_generic_records_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.mips;backend.native;feature.generic;feature.records;profile.o32;isa.vr4300;endian.big;endian.little;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
        add_test(NAME cross_runtime_generic_record_reflection_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/generic_record_reflection.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-record-reflection-${level}
                    -DENTRY=generic_record_reflection_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_record_reflection_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.x86_64;backend.native;feature.meta;feature.generic;feature.records;opt.${level}")
        add_test(NAME cross_mips_generic_record_reflection_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/generic_record_reflection.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/generic_record_reflection_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-record-reflection-mips-${level}
                    -DEXPECTED=P -DMIPS_CPUS=vr4300 -DMIPS_ENDIANS=b "-DCC_FLAGS=-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_mips_generic_record_reflection_${level} PROPERTIES
            LABELS "check.runtime;frontend;arch.mips;backend.native;feature.meta;feature.generic;feature.records;profile.o32;isa.vr4300;endian.big;opt.${level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_generic_records_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-records-errors
                -P "${CMAKE_SOURCE_DIR}/test/language/generic_records_errors.cmake")
    set_tests_properties(cross_generic_records_errors PROPERTIES
        LABELS "check.compile;diagnostics;frontend;arch.x86_64;feature.generic;feature.records")
    add_test(NAME cross_expression_core_llvm
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/expression_core.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/expression-core.ll
                -P "${CMAKE_SOURCE_DIR}/test/language/expression_core_llvm.cmake")
    set_tests_properties(cross_expression_core_llvm PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.llvm;feature.semantics")
    add_test(NAME cross_runtime_native_expression_core
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/expression_core.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-expression-core
                -DENTRY=expression_core_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_expression_core PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;opt.O2")
    add_test(NAME cross_runtime_native_expression_core_O0
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/expression_core.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-expression-core-O0
                -DENTRY=expression_core_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O0
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_expression_core_O0 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;opt.O0")
    foreach(adapter_mode O0 O2)
        add_test(NAME cross_function_pointer_adapter_${adapter_mode}
            COMMAND $<TARGET_FILE:cross_cc> -S -${adapter_mode} -mabi=ms_abi
                    "${CMAKE_SOURCE_DIR}/test/language/function_pointer_adapter.x"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/function-pointer-adapter-${adapter_mode}.s")
        set_tests_properties(cross_function_pointer_adapter_${adapter_mode} PROPERTIES
            LABELS "check.compile;arch.x86_64;backend.native;feature.calls;opt.${adapter_mode}")
    endforeach()
    add_test(NAME cross_function_pointer_adapter_mips64
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mprofile=mips64-n64
                "${CMAKE_SOURCE_DIR}/test/language/function_pointer_adapter_mips.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/function-pointer-adapter-mips64.s")
    set_tests_properties(cross_function_pointer_adapter_mips64 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.calls;opt.O2")
    add_test(NAME cross_runtime_native_function_pointer_adapter
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/function_pointer_adapter.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-function-pointer-adapter
                -DENTRY=function_pointer_adapter_entry
                -DEXPECTED=1
                -DCC_FLAGS=-O2
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_runtime_native_function_pointer_adapter PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.calls;opt.O2")
    add_test(NAME cross_forms_errors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/forms-errors
                -P "${CMAKE_SOURCE_DIR}/test/language/forms_errors.cmake")
    set_tests_properties(cross_forms_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.semantics;feature.attributes;feature.abi")
    add_test(NAME cross_attribute_brackets
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/attribute-brackets
                -P "${CMAKE_SOURCE_DIR}/test/language/attribute_brackets.cmake")
    set_tests_properties(cross_attribute_brackets PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.attributes;feature.syntax;feature.tokens")
    add_test(NAME cross_pointer_casts
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pointer-casts
                -P "${CMAKE_SOURCE_DIR}/test/language/pointer_casts.cmake")
    set_tests_properties(cross_pointer_casts PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.pointer;feature.semantics;feature.evaluation")
    add_test(NAME cross_pointer_integer_conversions
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pointer-integer-conversions
                -P "${CMAKE_SOURCE_DIR}/test/language/pointer_integer_conversions.cmake")
    set_tests_properties(cross_pointer_integer_conversions PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.pointer;feature.semantics;feature.evaluation;feature.initializer")
    foreach(conversion_level O0 O2)
        add_test(NAME cross_runtime_native_pointer_integer_conversions_${conversion_level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/pointer_integer_conversions.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-pointer-integer-conversions-${conversion_level}
                    -DENTRY=pointer_integer_conversions_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${conversion_level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_pointer_integer_conversions_${conversion_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.pointer;feature.semantics;feature.initializer;opt.${conversion_level}")
    endforeach()
    foreach(conversion_case "vr4300;b;big;O2" "r3000;l;little;O0")
        list(GET conversion_case 0 conversion_cpu)
        list(GET conversion_case 1 conversion_endian)
        list(GET conversion_case 2 conversion_order)
        list(GET conversion_case 3 conversion_level)
        set(name cross_runtime_mips_pointer_integer_conversions_${conversion_cpu}_${conversion_endian}_${conversion_level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/pointer_integer_conversions.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/pointer_integer_conversions_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-pointer-integer-conversions-${conversion_cpu}-${conversion_endian}-${conversion_level}
            -DMIPS_CPUS=${conversion_cpu} -DMIPS_ENDIANS=${conversion_endian} -DMIPS_SINGLE_CASE=ON
            -DEXPECTED=P -DCC_FLAGS=-${conversion_level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.pointer;feature.semantics;feature.initializer;profile.o32;isa.${conversion_cpu};endian.${conversion_order};opt.${conversion_level}")
    endforeach()
    add_test(NAME cross_compound_literals
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/compound-literals
                -P "${CMAKE_SOURCE_DIR}/test/language/compound_literals.cmake")
    set_tests_properties(cross_compound_literals PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;feature.initializer;feature.syntax_tree;feature.semantics")
    add_test(NAME cross_braced_initializers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/braced-initializers
                -P "${CMAKE_SOURCE_DIR}/test/language/braced_initializers.cmake")
    set_tests_properties(cross_braced_initializers PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;frontend;backend.native;feature.initializer;feature.semantics;feature.raw;feature.calls")
    foreach(forms_level O0 O2)
        add_test(NAME cross_runtime_native_forms_${forms_level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/forms.x
                    -DEXTRA_SOURCES=${CMAKE_SOURCE_DIR}/test/language/forms_table.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-forms-${forms_level}
                    -DENTRY=forms_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${forms_level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_forms_${forms_level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.semantics;feature.attributes;opt.${forms_level}")
    endforeach()
    foreach(forms_case "vr4300;b;big;O2" "r3000;l;little;O0")
        list(GET forms_case 0 forms_cpu)
        list(GET forms_case 1 forms_endian)
        list(GET forms_case 2 forms_order)
        list(GET forms_case 3 forms_level)
        set(name cross_forms_mips_${forms_cpu}_${forms_endian}_${forms_level})
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/forms.x
            -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/language/forms_table.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/language/forms_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/forms-mips-${forms_cpu}-${forms_endian}-${forms_level}
            -DMIPS_CPUS=${forms_cpu} -DMIPS_ENDIANS=${forms_endian} -DMIPS_SINGLE_CASE=ON
            -DEXPECTED=P -DCC_FLAGS=-${forms_level}
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(${name} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;feature.semantics;feature.attributes;profile.o32;isa.${forms_cpu};endian.${forms_order};opt.${forms_level}"
            SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endforeach()
    add_test(NAME cross_forms_mips64
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mprofile=mips64-n64
                "${CMAKE_SOURCE_DIR}/test/language/forms.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/forms-mips64.s")
    set_tests_properties(cross_forms_mips64 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.semantics;opt.O2")
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
    set_tests_properties(cross_runtime_native_addresses PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.pointer")
    add_test(NAME cross_fast_math
        COMMAND $<TARGET_FILE:cross_cc> -S -O3 -ffast-math
                "${CMAKE_SOURCE_DIR}/test/language/floating.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/floating.s")
    set_tests_properties(cross_fast_math PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.floating;opt.O3")
    add_test(NAME cross_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/abi.s")
    set_tests_properties(cross_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_default_cross_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross_default.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-default-abi
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross_default.cmake")
    set_tests_properties(cross_default_cross_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_cross_abi_separate_compilation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross/callee.x
                -DCALLER_SOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/cross/caller.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-abi-separate
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross/separate_compilation.cmake")
    set_tests_properties(cross_cross_abi_separate_compilation PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi")
    # Registers the Cross ABI preserves survive separately compiled calls.
    foreach(level O0 O1 O2 O3 Os Oz)
        add_test(NAME cross_cross_abi_preserved_${level}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DLEVEL=${level}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-abi-preserved-${level}
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross/preserved.cmake")
        set_tests_properties(cross_cross_abi_preserved_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.frame;opt.${level}")
    endforeach()
    # Each MIPS registration owns one ABI, CPU, and byte order.
    foreach(abi_cpu x32:r3000 x32:vr4300 x64:vr4300)
        string(REPLACE ":" ";" abi_cpu "${abi_cpu}")
        list(GET abi_cpu 0 abi)
        list(GET abi_cpu 1 cpu)
        foreach(endian big little)
            string(SUBSTRING ${endian} 0 1 endian_letter)
            set(name cross_cross_abi_preserved_mips_${abi}_${cpu}_${endian_letter})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc> -DABI=${abi}
                -DMIPS_CPUS=${cpu} -DMIPS_ENDIANS=${endian_letter}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cross-abi-preserved-mips-${abi}-${cpu}-${endian_letter}
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/cross/mips_preserved.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.abi;feature.frame;profile.${abi};isa.${cpu};endian.${endian}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_manual_register_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-abi.s")
    set_tests_properties(cross_manual_register_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O0")
    add_test(NAME cross_runtime_manual_register_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-abi.ll
                -DENTRY=manual_abi_entry
                -DEXPECTED=62
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_register_abi PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
    add_test(NAME cross_runtime_manual_simd_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-simd-abi.ll
                -DENTRY=manual_simd_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_simd_abi PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.vector")
    add_test(NAME cross_manual_simd_bit_transport
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_simd_transport.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-simd-transport.ll")
    set_tests_properties(cross_manual_simd_bit_transport PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.llvm;feature.abi;feature.vector;opt.O2")
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
    set_tests_properties(cross_runtime_native_manual_simd_bit_transport PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.vector")
    add_test(NAME cross_runtime_manual_simd_pointer_transport
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_simd_pointer_transport.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-simd-pointer-transport.ll
                -DENTRY=pointer_simd_entry
                -DEXPECTED=1
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_simd_pointer_transport PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.vector")
    add_test(NAME cross_manual_register_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_errors.cmake")
    set_tests_properties(cross_manual_register_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_clobber_contract
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-clobber-contract
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_clobber_contract.cmake")
    set_tests_properties(cross_manual_clobber_contract PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;middle;feature.abi")
    add_test(NAME cross_manual_register_sysv_optimizer
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_sysv_optimizer.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-sysv
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_sysv_optimizer.cmake")
    set_tests_properties(cross_manual_register_sysv_optimizer PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_register_pairs
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-pairs.s")
    set_tests_properties(cross_manual_register_pairs PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O0")
    add_test(NAME cross_runtime_manual_register_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-pairs.ll
                -DENTRY=paired_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_register_pairs PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
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
    set_tests_properties(cross_runtime_native_manual_register_pairs PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_register_pair_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-pair-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair_errors.cmake")
    set_tests_properties(cross_manual_register_pair_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_register_pairs_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-pairs-sysv.s")
    set_tests_properties(cross_manual_register_pairs_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_manual_register_indirect_pairs
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pairs.s")
    set_tests_properties(cross_manual_register_indirect_pairs PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.calls;opt.O0")
    add_test(NAME cross_runtime_manual_register_indirect_pairs
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-indirect-pairs.ll
                -DENTRY=indirect_pair_entry
                -DEXPECTED=116
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_register_indirect_pairs PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.calls")
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
    set_tests_properties(cross_runtime_native_manual_register_indirect_pairs PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.calls")
    add_test(NAME cross_manual_register_indirect_pair_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pair-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair_errors.cmake")
    set_tests_properties(cross_manual_register_indirect_pair_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi;feature.calls")
    add_test(NAME cross_manual_register_indirect_pairs_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_indirect_pair.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-indirect-pairs-sysv.s")
    set_tests_properties(cross_manual_register_indirect_pairs_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.calls;opt.O2")
    foreach(variant O0 O2 O2_noinline)
        string(REPLACE "_noinline" ";-fno-inline" manual_pointer_flags "-${variant}")
        string(REPLACE "_noinline" "" manual_pointer_level "${variant}")
        foreach(manual_pointer IN ITEMS "manual_indirect_call;manual_indirect_entry"
                                        "manual_adapter;manual_adapter_entry")
            list(GET manual_pointer 0 manual_pointer_source)
            list(GET manual_pointer 1 manual_pointer_entry)
            add_test(NAME cross_runtime_native_${manual_pointer_source}_${variant}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/${manual_pointer_source}.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-${manual_pointer_source}-${variant}
                        -DENTRY=${manual_pointer_entry}
                        -DEXPECTED=200
                        "-DCC_FLAGS=${manual_pointer_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_runtime_native_${manual_pointer_source}_${variant} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.calls;opt.${manual_pointer_level}")
        endforeach()
    endforeach()
    add_test(NAME cross_manual_indirect_call_codegen
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_indirect_call.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-indirect-call-codegen
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_indirect_codegen.cmake")
    set_tests_properties(cross_manual_indirect_call_codegen PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.register_allocation;opt.O2")
    add_test(NAME cross_runtime_manual_adapter
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_adapter.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-adapter.ll
                -DENTRY=manual_adapter_entry
                -DEXPECTED=200
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_adapter PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.calls")
    foreach(arch x86_64 mips)
        add_test(NAME cross_manual_indirect_diagnostics_${arch}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_indirect_errors.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-indirect-errors-${arch}
                    -DARCH=${arch}
                    -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_indirect_errors.cmake")
        set_tests_properties(cross_manual_indirect_diagnostics_${arch} PROPERTIES
            LABELS "check.compile;diagnostics;arch.${arch};backend.native;feature.abi;feature.calls")
    endforeach()
    add_test(NAME cross_manual_register_mixed
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed.s")
    set_tests_properties(cross_manual_register_mixed PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_runtime_manual_register_mixed
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-register-mixed.ll
                -DENTRY=mixed_endpoint_entry
                -DEXPECTED=123
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_register_mixed PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
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
    set_tests_properties(cross_runtime_native_manual_register_mixed PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_register_mixed_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed_errors.cmake")
    set_tests_properties(cross_manual_register_mixed_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_register_mixed_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_register_mixed.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-register-mixed-sysv.s")
    set_tests_properties(cross_manual_register_mixed_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_manual_stack_abi
        COMMAND $<TARGET_FILE:cross_cc> -S -O0
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-stack-abi.s")
    set_tests_properties(cross_manual_stack_abi PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O0")
    add_test(NAME cross_runtime_manual_stack_abi
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-stack-abi.ll
                -DENTRY=manual_stack_entry
                -DEXPECTED=74
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_stack_abi PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
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
    set_tests_properties(cross_runtime_native_manual_stack_abi PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_stack_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-stack-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_errors.cmake")
    set_tests_properties(cross_manual_stack_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_stack_abi_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_stack_abi.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-stack-abi-sysv.s")
    set_tests_properties(cross_manual_stack_abi_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_manual_memory_wide
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-memory-wide.s")
    set_tests_properties(cross_manual_memory_wide PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_runtime_manual_memory_wide
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-memory-wide.ll
                -DENTRY=manual_memory_wide_entry
                -DEXPECTED=9
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_memory_wide PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
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
    set_tests_properties(cross_runtime_native_manual_memory_wide PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_memory_wide_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_memory_wide.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-memory-wide-sysv.s")
    set_tests_properties(cross_manual_memory_wide_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_manual_aggregate
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_aggregate.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-aggregate.s")
    set_tests_properties(cross_manual_aggregate PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.records;opt.O2")
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
    set_tests_properties(cross_runtime_native_manual_aggregate PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.records")
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
    set_tests_properties(cross_runtime_native_manual_aggregate_o2 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.records;opt.O2")
    add_test(NAME cross_hard_register
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/hard-register.s")
    set_tests_properties(cross_hard_register PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_runtime_hard_register
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-hard-register.ll
                -DENTRY=hard_register_entry
                -DEXPECTED=11
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_hard_register PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi")
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
    set_tests_properties(cross_runtime_native_hard_register PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_hard_register_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hard-register-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register_errors.cmake")
    set_tests_properties(cross_hard_register_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_abi_placement_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/placement_errors.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/backend/abi/placement.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/abi-placement-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/placement_errors.cmake")
    set_tests_properties(cross_abi_placement_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi;feature.model")
    add_test(NAME cross_hard_register_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/hard_register.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/hard-register-sysv.s")
    set_tests_properties(cross_hard_register_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;opt.O2")
    add_test(NAME cross_manual_wide_simd
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mavx512f
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd.s")
    set_tests_properties(cross_manual_wide_simd PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.vector;opt.O2")
    add_test(NAME cross_manual_wide_simd_object
        COMMAND $<TARGET_FILE:cross_cc> -c -O2 -mavx512f
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd.o")
    set_tests_properties(cross_manual_wide_simd_object PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.vector;opt.O2")
    add_test(NAME cross_runtime_manual_wide_simd_vectors
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_vector_runtime.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-wide-simd.ll
                -DENTRY=wide_simd_vector_entry
                -DEXPECTED=2
                -DCC_FLAGS=-mavx
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_wide_simd_vectors PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.vector")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_wide_vector_calls_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/wide_vector_calls.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-wide-vector-calls-${level}
                    -DENTRY=wide_vector_entry
                    -DEXPECTED=200
                    "-DCC_FLAGS=-mavx;-${level}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_wide_vector_calls_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.calls;feature.vector;opt.${level}")
    endforeach()
    add_test(NAME cross_runtime_manual_ymm_scalar
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_ymm_scalar.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-ymm-scalar.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_ymm_runtime.cmake")
    set_tests_properties(cross_runtime_manual_ymm_scalar PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi")
    add_test(NAME cross_manual_wide_simd_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd-errors
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd_errors.cmake")
    set_tests_properties(cross_manual_wide_simd_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi;feature.vector")
    add_test(NAME cross_manual_wide_simd_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2 -mavx512f
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_wide_simd.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-wide-simd-sysv.s")
    set_tests_properties(cross_manual_wide_simd_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.vector;opt.O2")
    add_test(NAME cross_manual_x87
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-x87.s")
    set_tests_properties(cross_manual_x87 PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.floating;opt.O2")
    add_test(NAME cross_runtime_manual_x87
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-manual-x87.ll
                -DENTRY=x87_entry
                -DEXPECTED=23
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_manual_x87 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.abi;feature.floating")
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
    set_tests_properties(cross_runtime_native_manual_x87 PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.abi;feature.floating")
    add_test(NAME cross_manual_x87_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-x87-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_errors.cmake")
    set_tests_properties(cross_manual_x87_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;backend.native;feature.abi;feature.floating")
    add_test(NAME cross_manual_x87_sysv
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                -target x86_64-unknown-linux-gnu
                "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual-x87-sysv.s")
    set_tests_properties(cross_manual_x87_sysv PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.floating;opt.O2")
    add_test(NAME cross_manual_x87_standalone
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual-x87-standalone
                -P "${CMAKE_SOURCE_DIR}/test/backend/abi/manual_x87_standalone.cmake")
    set_tests_properties(cross_manual_x87_standalone PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.abi;feature.floating;feature.standalone")
    add_test(NAME cross_registers
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -P "${CMAKE_SOURCE_DIR}/test/backend/machine/register_registry.cmake")
    set_tests_properties(cross_registers PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.registers")
    add_test(NAME cross_compilation_group
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/group_decl.x"
                "${CMAKE_SOURCE_DIR}/test/language/group_def.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/group.s")
    set_tests_properties(cross_compilation_group PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.linkage;opt.O2")
    foreach(level O0 O2)
        add_test(NAME cross_group_types_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/test/language
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/group-types-${level}
                    -DLEVEL=${level}
                    -P "${CMAKE_SOURCE_DIR}/test/language/group_types.cmake")
        set_tests_properties(cross_group_types_${level} PROPERTIES
            LABELS "check.runtime;diagnostics;arch.x86_64;arch.mips;backend.native;feature.linkage;feature.types;feature.debug;opt.${level}")
    endforeach()
    add_test(NAME cross_source_unit_static
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/static_one.x"
                "${CMAKE_SOURCE_DIR}/test/language/static_two.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/static.s")
    set_tests_properties(cross_source_unit_static PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.linkage;opt.O2")
    add_test(NAME cross_labels
        COMMAND $<TARGET_FILE:cross_cc> -S -O2
                "${CMAKE_SOURCE_DIR}/test/language/labels.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/labels.s")
    set_tests_properties(cross_labels PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.labels;opt.O2")
    add_test(NAME cross_global_labels
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/global_labels.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/language/global_label_errors.x
                -DNOP_SOURCE=${CMAKE_SOURCE_DIR}/test/language/global_label_nop.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/global-labels
                -P "${CMAKE_SOURCE_DIR}/test/language/global_labels.cmake")
    set_tests_properties(cross_global_labels PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;feature.labels")
    add_test(NAME cross_musttail
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail.x
                -DPARSER_ERRORS=${CMAKE_SOURCE_DIR}/test/language/musttail_parser_errors.x
                -DLOWERING_ERRORS=${CMAKE_SOURCE_DIR}/test/language/musttail_lowering_errors.x
                -DSTACK_ERROR=${CMAKE_SOURCE_DIR}/test/language/musttail_stack_error.x
                -DHOME_ERROR=${CMAKE_SOURCE_DIR}/test/language/musttail_home_error.x
                -DREUSE_HOME_SOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_reuse_home.x
                -DCUSTOM_RESULT_ERROR=${CMAKE_SOURCE_DIR}/test/language/musttail_custom_result_error.x
                -DCUSTOM_RESULT_SOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail_custom_result.x
                -DCUSTOM_MODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                -DCLOBBER_ERROR=${CMAKE_SOURCE_DIR}/test/language/musttail_clobber_error.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/musttail
                -P "${CMAKE_SOURCE_DIR}/test/language/musttail.cmake")
    set_tests_properties(cross_musttail PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;feature.musttail;profile.custom")
    add_test(NAME cross_musttail_runtime
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/musttail.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DENTRY=musttail_entry -DEXPECTED=42
                "-DCC_FLAGS=-O2;-fno-optimize-sibling-calls;-fno-eval-calls"
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/musttail-runtime
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
    set_tests_properties(cross_musttail_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;feature.musttail;opt.O2")
    add_test(NAME cross_preprocessor
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/preprocessor.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/preprocessor.i
                -P "${CMAKE_SOURCE_DIR}/test/frontend/preprocessor.cmake")
    set_tests_properties(cross_preprocessor PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.preprocessor")
    add_executable(cross_preprocessor_expression_test test/frontend/preprocessor_expression.cpp)
    target_link_libraries(cross_preprocessor_expression_test PRIVATE cross_frontend)
    cross_component(cross_preprocessor_expression_test)
    add_test(NAME cross_preprocessor_expression COMMAND cross_preprocessor_expression_test)
    set_tests_properties(cross_preprocessor_expression PROPERTIES
        LABELS "check.unit;arch.independent;feature.preprocessor")
    add_test(NAME cross_preprocessor_conditions
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/preprocessor_conditions.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/preprocessor-conditions
                -P "${CMAKE_SOURCE_DIR}/test/frontend/preprocessor_conditions.cmake")
    set_tests_properties(cross_preprocessor_conditions PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.preprocessor")
    add_test(NAME cross_preprocessor_dependencies
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/preprocessor-dependencies
                -P "${CMAKE_SOURCE_DIR}/test/frontend/preprocessor_dependencies.cmake")
    set_tests_properties(cross_preprocessor_dependencies PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.preprocessor")
    add_test(NAME cross_standard_output
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/standard-output
                -P "${CMAKE_SOURCE_DIR}/test/frontend/standard_output.cmake")
    set_tests_properties(cross_standard_output PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.driver;feature.preprocessor")
    add_test(NAME cross_forced_includes
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/forced-includes
                -P "${CMAKE_SOURCE_DIR}/test/frontend/forced_includes.cmake")
    set_tests_properties(cross_forced_includes PROPERTIES
        LABELS "frontend;feature.preprocessor;arch.x86_64;check.compile;diagnostics")
    add_test(NAME cross_reproducible_output
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/reproducible-output
                -P "${CMAKE_SOURCE_DIR}/test/frontend/reproducible_output.cmake")
    set_tests_properties(cross_reproducible_output PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;backend.native;feature.driver;feature.linkage;feature.object")
    add_test(NAME cross_macro_invocation_lines
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/macro_lines.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/macro-lines
                -P "${CMAKE_SOURCE_DIR}/test/frontend/macro_lines.cmake")
    set_tests_properties(cross_macro_invocation_lines PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.preprocessor;diagnostics")
    add_test(NAME cross_keyword_negation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/keyword_negation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/keyword-negation
                -P "${CMAKE_SOURCE_DIR}/test/frontend/keyword_negation.cmake")
    set_tests_properties(cross_keyword_negation PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.procedural")
    add_test(NAME cross_generic_budgets
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-budgets
                -P "${CMAKE_SOURCE_DIR}/test/frontend/generic_budgets.cmake")
    set_tests_properties(cross_generic_budgets PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.generic;feature.resources;diagnostics")
    add_test(NAME cross_embed_values
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/embed_values.x
                -DDRIVER=${CMAKE_SOURCE_DIR}/test/frontend/embed_values_driver.cpp
                -DGENERATOR=${CMAKE_SOURCE_DIR}/test/frontend/embed_values_generator.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embed-values
                -P "${CMAKE_SOURCE_DIR}/test/frontend/embed_values.cmake")
    set_tests_properties(cross_embed_values PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.preprocessor")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_tsv_enum_${level}
                 COMMAND "${CMAKE_COMMAND}"
                         -DCC=$<TARGET_FILE:cross_cc>
                         -DHOST_CXX=${CMAKE_CXX_COMPILER}
                         -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/tsv_enum.x
                         -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                         -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tsv-enum-${level}
                         -DENTRY=tsv_enum_entry
                         -DEXPECTED=7
                         "-DCC_FLAGS=-${level}"
                         -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_tsv_enum_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;frontend;feature.macro;feature.enum;feature.preprocessor;opt.${level}")
    endforeach()
    add_test(NAME cross_meta_object_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/meta_objects.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-object-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_objects.cmake")
    set_tests_properties(cross_meta_object_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta")
    add_test(NAME cross_syntax_raw_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-raw-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw.cmake")
    foreach(raw_target mips mipsel mips64 mips64el)
        if(raw_target MATCHES "^mips64")
            set(raw_width 8)
        else()
            set(raw_width 4)
        endif()
        if(raw_target MATCHES "el$")
            set(raw_endian little)
        else()
            set(raw_endian big)
        endif()
        add_test(NAME cross_syntax_raw_target_${raw_target}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-raw-target-${raw_target}.s
                -DTARGET=${raw_target}-unknown-linux-gnu -DWIDTH=${raw_width}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_target.cmake")
        cross_syntax_test_labels(cross_syntax_raw_target_${raw_target} mips ${raw_target}
            endian.${raw_endian} check.compile)
    endforeach()
    add_test(NAME cross_syntax_patch_identity_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DHOST_CXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-patch-identity-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.cmake")
    foreach(patch_level O0 O2)
        foreach(patch_target host elf r3000 allegrex n64)
            if(patch_target STREQUAL host OR patch_target STREQUAL elf)
                set(patch_arch x86_64)
            else()
                set(patch_arch mips)
            endif()
            add_test(NAME cross_syntax_patch_cells_${patch_target}_${patch_level}
                COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.x
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-patch-cells-${patch_target}-${patch_level}
                    -DTARGET_MODE=${patch_target} -DLEVEL=${patch_level}
                    -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_cells.cmake")
            cross_syntax_test_labels(cross_syntax_patch_cells_${patch_target}_${patch_level}
                ${patch_arch} ${patch_target} feature.patch check.compile opt.${patch_level})
        endforeach()
    endforeach()
    if(CROSS_ENABLE_LLVM_TEXT)
        add_test(NAME cross_syntax_patch_llvm
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-patch-llvm
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_llvm.cmake")
        cross_syntax_test_labels(cross_syntax_patch_llvm x86_64 native
            feature.patch backend.llvm check.runtime)
        set_tests_properties(cross_syntax_patch_llvm PROPERTIES
            SKIP_REGULAR_EXPRESSION "skipping optional LLVM patch execution")
    endif()
    add_test(NAME cross_syntax_span_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-span-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_spans.cmake")
    add_test(NAME cross_warnings
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc> -DCPP=$<TARGET_FILE:cross_cpp>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/warnings
            -P "${CMAKE_SOURCE_DIR}/test/frontend/warnings.cmake")
    set_tests_properties(cross_warnings PROPERTIES
        LABELS "frontend;diagnostics;feature.warnings;arch.x86_64;check.compile")
    add_test(NAME cross_call_checkers
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/call-checkers
            -P "${CMAKE_SOURCE_DIR}/test/frontend/call_checkers.cmake")
    set_tests_properties(cross_call_checkers PROPERTIES
        LABELS "frontend;diagnostics;feature.warnings;feature.meta;arch.x86_64;check.compile")
    foreach(arch x86_64 mips)
        if(arch STREQUAL x86_64)
            set(warning_target x86_64-unknown-linux-gnu)
        else()
            set(warning_target mips-unknown-elf)
        endif()
        add_test(NAME cross_format_checks_${arch}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc> -DARCH=${arch}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/format-checks-${arch}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/format_checks.cmake")
        set_tests_properties(cross_format_checks_${arch} PROPERTIES
            LABELS "frontend;diagnostics;feature.warnings;feature.format;arch.${arch};check.compile")
        add_test(NAME cross_warning_output_${arch}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc> -DTARGET=${warning_target}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/warning-output-${arch}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/warning_output.cmake")
        set_tests_properties(cross_warning_output_${arch} PROPERTIES
            LABELS "frontend;feature.warnings;backend.native;arch.${arch};check.compile")
    endforeach()
    add_test(NAME cross_syntax_origin_errors
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_origin_errors.x
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-origin-errors.s
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_origin_errors.cmake")
    add_test(NAME cross_syntax_activation_errors
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-activation-errors.s
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_activation_errors.cmake")
    add_test(NAME cross_syntax_context_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-context-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_contexts.cmake")
    foreach(storage_mode native custom mips mipsel mips64 mips64el)
        if(storage_mode MATCHES "^mips")
            set(storage_arch mips)
        else()
            set(storage_arch x86_64)
        endif()
        add_test(NAME cross_syntax_retained_storage_${storage_mode}
            COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-retained-storage-${storage_mode}
                -DMODE=${storage_mode}
                -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_retained_storage.cmake")
        cross_syntax_test_labels(cross_syntax_retained_storage_${storage_mode}
            ${storage_arch} ${storage_mode} feature.meta feature.resources diagnostics)
    endforeach()
    add_test(NAME cross_meta_helper_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-helper-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_helpers.cmake")
    set_tests_properties(cross_meta_helper_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta")
    add_test(NAME cross_syntax_deferred_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-deferred-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_deferred.cmake")
    add_test(NAME cross_syntax_hygiene_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-hygiene-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_hygiene.cmake")
    add_test(NAME cross_syntax_splice_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-splice-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_splice.cmake")
    add_test(NAME cross_syntax_header_checks
        COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-header-checks
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_header.cmake")
    add_executable(cross_syntax_state_test test/frontend/syntax_state_test.cpp)
    target_link_libraries(cross_syntax_state_test PRIVATE cross_frontend)
    cross_component(cross_syntax_state_test)
    add_test(NAME cross_syntax_state COMMAND cross_syntax_state_test)
    add_executable(cross_syntax_matcher_test test/frontend/syntax_matcher_test.cpp)
    target_link_libraries(cross_syntax_matcher_test PRIVATE cross_frontend)
    cross_component(cross_syntax_matcher_test)
    add_test(NAME cross_syntax_matcher COMMAND cross_syntax_matcher_test)
    cross_syntax_test_labels(cross_syntax_matcher independent none check.unit feature.matcher)
    add_executable(cross_syntax_recognition_test test/frontend/syntax_recognition_test.cpp)
    target_link_libraries(cross_syntax_recognition_test PRIVATE cross_frontend)
    cross_component(cross_syntax_recognition_test)
    add_test(NAME cross_syntax_recognition COMMAND cross_syntax_recognition_test)
    cross_syntax_test_labels(cross_syntax_recognition independent none check.unit feature.matcher feature.resources)
    add_executable(cross_syntax_execution_test test/frontend/syntax_execution_test.cpp)
    target_link_libraries(cross_syntax_execution_test PRIVATE cross_frontend)
    cross_component(cross_syntax_execution_test)
    add_test(NAME cross_syntax_execution COMMAND cross_syntax_execution_test)
    cross_syntax_test_labels(cross_syntax_execution independent none check.unit feature.evaluation feature.resources feature.macro)
    add_executable(cross_syntax_validation_test test/frontend/syntax_validation_test.cpp)
    target_link_libraries(cross_syntax_validation_test PRIVATE cross_frontend)
    cross_component(cross_syntax_validation_test)
    add_test(NAME cross_syntax_validation_state COMMAND cross_syntax_validation_test)
    set_tests_properties(cross_syntax_validation_state PROPERTIES
        LABELS "frontend;feature.syntax;feature.syntax_validation;arch.independent")
    add_executable(cross_patch_capabilities_test test/frontend/patch_capabilities_test.cpp)
    target_link_libraries(cross_patch_capabilities_test PRIVATE cross_frontend)
    cross_component(cross_patch_capabilities_test)
    add_test(NAME cross_patch_capabilities COMMAND cross_patch_capabilities_test)
    set_tests_properties(cross_patch_capabilities PROPERTIES
        LABELS "frontend;feature.patch;feature.source_constraints;arch.independent;check.unit")
    add_executable(cross_evaluation_context_test test/frontend/evaluation_context_test.cpp)
    target_link_libraries(cross_evaluation_context_test PRIVATE cross_frontend)
    cross_component(cross_evaluation_context_test)
    add_test(NAME cross_evaluation_context COMMAND cross_evaluation_context_test)
    set_tests_properties(cross_evaluation_context PROPERTIES
        LABELS "frontend;feature.evaluation;feature.meta;arch.independent;check.unit")
    add_executable(cross_evaluation_layout_test test/frontend/evaluation_layout_test.cpp)
    target_link_libraries(cross_evaluation_layout_test PRIVATE cross_frontend)
    cross_component(cross_evaluation_layout_test)
    add_test(NAME cross_evaluation_layout COMMAND cross_evaluation_layout_test)
    set_tests_properties(cross_evaluation_layout PROPERTIES
        LABELS "frontend;feature.evaluation;feature.meta;feature.layout;arch.independent;check.unit")
    add_executable(cross_constant_finalization_test test/frontend/constant_finalization_test.cpp)
    target_link_libraries(cross_constant_finalization_test PRIVATE cross_frontend)
    cross_component(cross_constant_finalization_test)
    add_test(NAME cross_constant_finalization COMMAND $<TARGET_FILE:cross_constant_finalization_test>)
    set_tests_properties(cross_constant_finalization PROPERTIES
        LABELS "frontend;feature.evaluation;feature.relocation;feature.patch;feature.resources;arch.independent;check.unit")
    add_executable(cross_evaluation_rewriting_test test/frontend/evaluation_rewriting_test.cpp)
    target_link_libraries(cross_evaluation_rewriting_test PRIVATE cross_frontend)
    cross_component(cross_evaluation_rewriting_test)
    add_test(NAME cross_evaluation_rewriting COMMAND $<TARGET_FILE:cross_evaluation_rewriting_test>)
    set_tests_properties(cross_evaluation_rewriting PROPERTIES
        LABELS "frontend;feature.evaluation;feature.meta;feature.hygiene;feature.resources;arch.independent;check.unit")
    add_executable(cross_evaluation_enumerators_test test/frontend/evaluation_enumerators_test.cpp)
    target_link_libraries(cross_evaluation_enumerators_test PRIVATE cross_frontend)
    cross_component(cross_evaluation_enumerators_test)
    add_test(NAME cross_evaluation_enumerators COMMAND cross_evaluation_enumerators_test)
    set_tests_properties(cross_evaluation_enumerators PROPERTIES
        LABELS "frontend;feature.evaluation;feature.enum;feature.resources;arch.independent;check.unit")
    add_executable(cross_evaluation_generics_test test/frontend/evaluation_generics_test.cpp)
    target_link_libraries(cross_evaluation_generics_test PRIVATE cross_frontend)
    cross_component(cross_evaluation_generics_test)
    add_test(NAME cross_evaluation_generics COMMAND cross_evaluation_generics_test)
    set_tests_properties(cross_evaluation_generics PROPERTIES
        LABELS "frontend;feature.evaluation;feature.generic;feature.array;feature.resources;arch.independent;check.unit")
    add_executable(cross_syntax_relocation_test test/frontend/syntax_relocation_test.cpp)
    target_link_libraries(cross_syntax_relocation_test PRIVATE cross_frontend)
    cross_component(cross_syntax_relocation_test)
    add_test(NAME cross_syntax_relocation_state COMMAND cross_syntax_relocation_test)
    set_tests_properties(cross_syntax_relocation_state PROPERTIES
        LABELS "frontend;feature.syntax;feature.hygiene;arch.independent")
    add_executable(cross_meta_token_api_test test/frontend/meta_token_api_test.cpp)
    target_link_libraries(cross_meta_token_api_test PRIVATE cross_frontend)
    cross_component(cross_meta_token_api_test)
    add_test(NAME cross_meta_token_api_state COMMAND cross_meta_token_api_test)
    set_tests_properties(cross_meta_token_api_state PROPERTIES
        LABELS "frontend;feature.meta;feature.tokens;feature.syntax;arch.independent")
    add_executable(cross_syntax_schema_test test/frontend/syntax_schema_test.cpp)
    target_link_libraries(cross_syntax_schema_test PRIVATE cross_frontend)
    cross_component(cross_syntax_schema_test)
    add_test(NAME cross_syntax_schema COMMAND cross_syntax_schema_test)
    add_executable(cross_syntax_release_test test/frontend/syntax_release_test.cpp)
    target_link_libraries(cross_syntax_release_test PRIVATE cross_frontend)
    cross_component(cross_syntax_release_test)
    add_test(NAME cross_syntax_release COMMAND cross_syntax_release_test)
    cross_syntax_test_labels(cross_syntax_release independent none check.unit feature.syntax_tree feature.resources)
    add_executable(cross_provenance_release_test test/frontend/provenance_release_test.cpp)
    target_link_libraries(cross_provenance_release_test PRIVATE cross_frontend)
    cross_component(cross_provenance_release_test)
    add_test(NAME cross_provenance_release COMMAND cross_provenance_release_test)
    cross_syntax_test_labels(cross_provenance_release independent none check.unit feature.provenance feature.resources)
    add_executable(cross_evaluation_task_test test/frontend/evaluation_task_test.cpp)
    cross_component(cross_evaluation_task_test)
    add_test(NAME cross_evaluation_task COMMAND cross_evaluation_task_test)
    set_tests_properties(cross_evaluation_task PROPERTIES
        LABELS "frontend;feature.meta;feature.evaluation;feature.syntax;arch.independent;check.unit")
    add_test(NAME cross_syntax_declaration_checks
        COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-declarations
            -P "${CMAKE_SOURCE_DIR}/test/frontend/syntax_declarations.cmake")
    foreach(syntax_check raw_checks patch_identity_checks span_checks origin_errors
            activation_errors context_checks deferred_checks hygiene_checks
            splice_checks header_checks declaration_checks)
        cross_syntax_test_labels(cross_syntax_${syntax_check} x86_64 native diagnostics)
    endforeach()
    cross_syntax_test_labels(cross_syntax_state independent none check.unit)
    cross_syntax_test_labels(cross_syntax_schema independent none check.unit feature.syntax_tree)
    if(WIN32)
        set(meta_vector_host_abi ms_abi)
    else()
        set(meta_vector_host_abi sysv_abi)
    endif()
    foreach(level O0 O2)
        foreach(mode normal noeval custom)
            set(meta_vector_flags -${level})
            if(mode STREQUAL noeval)
                list(APPEND meta_vector_flags -fno-eval-calls)
            elseif(mode STREQUAL custom)
                list(APPEND meta_vector_flags
                    "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y" -mabi=odd_abi
                    -DCUSTOM_META_ABI "-DHOST_ABI=\"${meta_vector_host_abi}\"")
            endif()
            set(syntax_flags ${meta_vector_flags})
            set(eval_x87_flags -${level} "-DHOST_ABI=\"${meta_vector_host_abi}\"")
            if(mode STREQUAL noeval)
                list(APPEND eval_x87_flags -fno-eval-calls)
            elseif(mode STREQUAL custom)
                list(APPEND eval_x87_flags "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y"
                    -mabi=odd_abi -DCUSTOM_EVAL_ABI -fno-eval-calls)
            endif()
            add_test(NAME cross_evaluation_outputs_x87_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}" -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/evaluation_outputs_x87.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evaluation-outputs-x87-${level}-${mode}
                    -DENTRY=syntax_raw_entry -DEXPECTED=61 "-DCC_FLAGS=${eval_x87_flags}"
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_evaluation_outputs_x87_${level}_${mode} PROPERTIES
                LABELS "frontend;feature.meta;feature.parameters;feature.floating;arch.x86_64;profile.${mode};check.runtime;backend.native;opt.${level}")
            if(mode STREQUAL custom)
                list(APPEND syntax_flags -DCUSTOM_SYNTAX_ABI)
            endif()
            foreach(patch_runner baseline rewritten)
                if(patch_runner STREQUAL baseline)
                    set(patch_runner_source test/support/native_entry.cpp)
                else()
                    set(patch_runner_source test/frontend/syntax_patch_identity_runner.cpp)
                endif()
                add_test(NAME cross_syntax_patch_identity_${level}_${mode}_${patch_runner}
                    COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/${patch_runner_source}
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-patch-${level}-${mode}-${patch_runner}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            endforeach()
            add_test(NAME cross_syntax_raw_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-raw-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_syntax_deferred_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_deferred.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-deferred-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_syntax_hygiene_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_hygiene.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-hygiene-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags};-DTEST_RAW_LABELS"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_gensym_global_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_global.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-global-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_gensym_categories_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_categories.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-categories-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_gensym_global_labels_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_global_labels.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-global-labels-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            add_test(NAME cross_syntax_splice_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_splice.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-splice-${level}-${mode}
                        -DENTRY=syntax_raw_entry -DEXPECTED=61
                        "-DCC_FLAGS=${syntax_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            foreach(syntax_fixture syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments meta_record_values meta_switch_values meta_lexical_context meta_namespace_imports meta_operator_values meta_continuations)
                string(REPLACE "_" "-" syntax_fixture_output "${syntax_fixture}")
                set(syntax_fixture_flags ${syntax_flags})
                if(syntax_fixture MATCHES "^syntax_(generic|type|layout|item)_depth$")
                    list(APPEND syntax_fixture_flags -feval-step-limit=16000000)
                endif()
                if(syntax_fixture STREQUAL "syntax_recognition_depth")
                    list(APPEND syntax_fixture_flags -feval-step-limit=100000000
                        -feval-byte-limit=268435456 -feval-memory-limit=268435456)
                endif()
                set(syntax_fixture_runner "${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp")
                if(syntax_fixture STREQUAL "meta_switch_values")
                    set(syntax_fixture_runner "${CMAKE_SOURCE_DIR}/test/frontend/meta_nested_switch_reference.cpp")
                endif()
                add_test(NAME cross_${syntax_fixture}_${level}_${mode}
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DHOST_CXX=${CMAKE_CXX_COMPILER}
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${syntax_fixture}.x
                            -DRUNNER=${syntax_fixture_runner}
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${syntax_fixture_output}-${level}-${mode}
                            -DENTRY=syntax_raw_entry -DEXPECTED=61
                            "-DCC_FLAGS=${syntax_fixture_flags}"
                            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                if(syntax_fixture STREQUAL "syntax_header_nominals")
                    set_tests_properties(cross_${syntax_fixture}_${level}_${mode} PROPERTIES
                        LABELS "frontend;feature.syntax;feature.generic;feature.nominal;arch.x86_64;profile.${mode}")
                endif()
            endforeach()
            if(mode STREQUAL "custom")
                add_test(NAME cross_syntax_splice_${level}_custom_noeval
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DHOST_CXX=${CMAKE_CXX_COMPILER}
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_splice.x
                            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-splice-${level}-custom-noeval
                            -DENTRY=syntax_raw_entry -DEXPECTED=61
                            "-DCC_FLAGS=${syntax_flags};-fno-eval-calls"
                            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                add_test(NAME cross_syntax_hygiene_${level}_custom_noeval
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DHOST_CXX=${CMAKE_CXX_COMPILER}
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_hygiene.x
                            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-hygiene-${level}-custom-noeval
                            -DENTRY=syntax_raw_entry -DEXPECTED=61
                            "-DCC_FLAGS=${syntax_flags};-fno-eval-calls;-DTEST_RAW_LABELS"
                            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                foreach(syntax_fixture syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments meta_record_values meta_switch_values meta_lexical_context meta_namespace_imports meta_operator_values meta_continuations)
                    string(REPLACE "_" "-" syntax_fixture_output "${syntax_fixture}")
                    set(syntax_fixture_flags ${syntax_flags})
                    if(syntax_fixture MATCHES "^syntax_(generic|type|layout|item)_depth$")
                        list(APPEND syntax_fixture_flags -feval-step-limit=16000000)
                    endif()
                    if(syntax_fixture STREQUAL "syntax_recognition_depth")
                        list(APPEND syntax_fixture_flags -feval-step-limit=100000000
                            -feval-byte-limit=268435456 -feval-memory-limit=268435456)
                    endif()
                    set(syntax_fixture_runner "${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp")
                    if(syntax_fixture STREQUAL "meta_switch_values")
                        set(syntax_fixture_runner "${CMAKE_SOURCE_DIR}/test/frontend/meta_nested_switch_reference.cpp")
                    endif()
                    add_test(NAME cross_${syntax_fixture}_${level}_custom_noeval
                        COMMAND "${CMAKE_COMMAND}"
                                -DCC=$<TARGET_FILE:cross_cc>
                                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${syntax_fixture}.x
                                -DRUNNER=${syntax_fixture_runner}
                                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${syntax_fixture_output}-${level}-custom-noeval
                                -DENTRY=syntax_raw_entry -DEXPECTED=61
                                "-DCC_FLAGS=${syntax_fixture_flags};-fno-eval-calls"
                                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                    if(syntax_fixture STREQUAL "syntax_header_nominals")
                        set_tests_properties(cross_${syntax_fixture}_${level}_custom_noeval PROPERTIES
                            LABELS "frontend;feature.syntax;feature.generic;feature.nominal;arch.x86_64;profile.custom_noeval")
                    endif()
                endforeach()
                add_test(NAME cross_gensym_global_labels_${level}_custom_noeval
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DHOST_CXX=${CMAKE_CXX_COMPILER}
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_global_labels.x
                            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-global-labels-${level}-custom-noeval
                            -DENTRY=syntax_raw_entry -DEXPECTED=61
                            "-DCC_FLAGS=${syntax_flags};-fno-eval-calls"
                            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            endif()
            foreach(update_kind vector_lanes compound_updates array_bounds)
                if(update_kind STREQUAL "vector_lanes")
                    set(update_source vector_lane_updates.x)
                else()
                    set(update_source ${update_kind}.x)
                endif()
                add_test(NAME cross_syntax_${update_kind}_${level}_${mode}
                    COMMAND "${CMAKE_COMMAND}"
                            -DCC=$<TARGET_FILE:cross_cc>
                            -DHOST_CXX=${CMAKE_CXX_COMPILER}
                            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${update_source}
                            -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-${update_kind}-${level}-${mode}
                            -DENTRY=syntax_raw_entry -DEXPECTED=61
                            "-DCC_FLAGS=${syntax_flags};-DTEST_NARROW_ATOMICS"
                            -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                if(mode STREQUAL "custom")
                    add_test(NAME cross_syntax_${update_kind}_${level}_custom_noeval
                        COMMAND "${CMAKE_COMMAND}"
                                -DCC=$<TARGET_FILE:cross_cc>
                                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${update_source}
                                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-${update_kind}-${level}-custom-noeval
                                -DENTRY=syntax_raw_entry -DEXPECTED=61
                                "-DCC_FLAGS=${syntax_flags};-fno-eval-calls;-DTEST_NARROW_ATOMICS"
                                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                endif()
                if(update_kind STREQUAL "array_bounds" AND level STREQUAL "O2" AND
                   (mode STREQUAL "normal" OR mode STREQUAL "custom"))
                    add_test(NAME cross_syntax_array_bounds_O2_${mode}_noalloc
                        COMMAND "${CMAKE_COMMAND}"
                                -DCC=$<TARGET_FILE:cross_cc>
                                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/array_bounds.x
                                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-array-bounds-O2-${mode}-noalloc
                                -DENTRY=syntax_raw_entry -DEXPECTED=61
                                "-DCC_FLAGS=${syntax_flags};-fno-eval-calls;-fno-register-allocation"
                                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
                endif()
            endforeach()
            foreach(syntax_fixture syntax_raw syntax_deferred syntax_hygiene syntax_splice
                    gensym_global gensym_categories gensym_global_labels
                    syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments
                    meta_record_values meta_switch_values meta_lexical_context
                    meta_namespace_imports meta_operator_values meta_continuations)
                cross_syntax_test_labels(cross_${syntax_fixture}_${level}_${mode}
                    x86_64 ${mode} check.runtime backend.native opt.${level})
            endforeach()
            foreach(patch_runner baseline rewritten)
                cross_syntax_test_labels(cross_syntax_patch_identity_${level}_${mode}_${patch_runner}
                    x86_64 ${mode} feature.patch check.runtime backend.native opt.${level})
            endforeach()
            foreach(update_kind vector_lanes compound_updates array_bounds)
                cross_syntax_test_labels(cross_syntax_${update_kind}_${level}_${mode}
                    x86_64 ${mode} feature.${update_kind} check.runtime backend.native opt.${level})
                if(mode STREQUAL custom)
                    cross_syntax_test_labels(cross_syntax_${update_kind}_${level}_custom_noeval
                        x86_64 custom_noeval feature.${update_kind} check.runtime backend.native opt.${level})
                endif()
            endforeach()
            if(level STREQUAL O2 AND (mode STREQUAL normal OR mode STREQUAL custom))
                cross_syntax_test_labels(cross_syntax_array_bounds_O2_${mode}_noalloc
                    x86_64 ${mode} feature.array_bounds check.runtime backend.native opt.O2)
            endif()
            if(mode STREQUAL custom)
                foreach(syntax_fixture syntax_splice syntax_hygiene syntax_generic_tags
                        syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments gensym_global_labels
                        meta_record_values meta_switch_values meta_lexical_context
                        meta_namespace_imports meta_operator_values meta_continuations)
                    cross_syntax_test_labels(cross_${syntax_fixture}_${level}_custom_noeval
                        x86_64 custom_noeval check.runtime backend.native opt.${level})
                endforeach()
            endif()
            add_test(NAME cross_meta_objects_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/meta_objects.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-objects-${level}-${mode}
                        -DENTRY=meta_object_entry -DEXPECTED=53
                        "-DCC_FLAGS=${meta_vector_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_meta_objects_${level}_${mode} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;frontend;feature.meta;opt.${level}")
            add_test(NAME cross_meta_vectors_${level}_${mode}
                COMMAND "${CMAKE_COMMAND}"
                        -DCC=$<TARGET_FILE:cross_cc>
                        -DHOST_CXX=${CMAKE_CXX_COMPILER}
                        -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/meta_vectors.x
                        -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-vectors-${level}-${mode}
                        -DENTRY=meta_vector_entry
                        -DEXPECTED=47
                        "-DCC_FLAGS=${meta_vector_flags}"
                        -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
            set_tests_properties(cross_meta_vectors_${level}_${mode} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;frontend;feature.meta;feature.vector;opt.${level}")
        endforeach()
    endforeach()
    # Each runtime registration owns one CPU, byte order and output prefix.
    # The aggregate syntax label still selects the complete cross-target suite.
    function(cross_syntax_o32_runtime fixture source level cpu endian)
        if(endian STREQUAL b)
            set(endian_label big)
        elseif(endian STREQUAL l)
            set(endian_label little)
        else()
            message(FATAL_ERROR "invalid syntax o32 byte order: ${endian}")
        endif()
        set(name cross_${fixture}_${cpu}_${endian}_${level})
        set(flags -${level} -fno-eval-calls ${ARGN})
        if(fixture MATCHES "^syntax_(generic|type|layout|item)_depth$")
            list(APPEND flags -feval-step-limit=16000000)
        endif()
        if(fixture STREQUAL "syntax_recognition_depth")
            list(APPEND flags -feval-step-limit=100000000
                -feval-byte-limit=268435456 -feval-memory-limit=268435456)
        endif()
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${source}.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${fixture}-${cpu}-${endian}-${level}
            -DMIPS_CPUS=${cpu} -DMIPS_ENDIANS=${endian} -DMIPS_SINGLE_CASE=ON
            -DEXPECTED=P "-DCC_FLAGS=${flags}"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        cross_syntax_test_labels(${name} mips o32 check.runtime backend.native
            opt.${level} isa.${cpu} endian.${endian_label})
        if(fixture STREQUAL syntax_header_nominals)
            set_property(TEST ${name} APPEND PROPERTY LABELS feature.generic feature.nominal)
        elseif(fixture MATCHES "^syntax_compound_updates")
            set_property(TEST ${name} APPEND PROPERTY LABELS feature.compound_updates)
        endif()
        set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "skipping MIPS")
    endfunction()
    foreach(syntax_level O0 O2)
        foreach(syntax_cpu r3000 vr4300)
            foreach(syntax_byte_order b l)
                foreach(syntax_fixture syntax_patch_identity syntax_array_bounds syntax_vector_lanes
                        syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments
                        meta_record_values meta_switch_values meta_lexical_context meta_namespace_imports
                        meta_operator_values meta_continuations syntax_splice syntax_hygiene gensym_global
                        gensym_categories gensym_global_labels syntax_deferred syntax_raw)
                    set(syntax_source ${syntax_fixture})
                    if(syntax_fixture STREQUAL syntax_array_bounds)
                        set(syntax_source array_bounds)
                    elseif(syntax_fixture STREQUAL syntax_vector_lanes)
                        set(syntax_source vector_lane_updates)
                    endif()
                    cross_syntax_o32_runtime(${syntax_fixture} ${syntax_source} ${syntax_level}
                        ${syntax_cpu} ${syntax_byte_order})
                endforeach()
                if(syntax_cpu STREQUAL r3000)
                    cross_syntax_o32_runtime(syntax_compound_updates compound_updates ${syntax_level}
                        ${syntax_cpu} ${syntax_byte_order} -DTEST_INTEGER_ONLY)
                else()
                    cross_syntax_o32_runtime(syntax_compound_updates compound_updates ${syntax_level}
                        ${syntax_cpu} ${syntax_byte_order} -mno-llsc)
                    cross_syntax_o32_runtime(syntax_compound_updates_llsc compound_updates ${syntax_level}
                        ${syntax_cpu} ${syntax_byte_order} -mllsc)
                endif()
            endforeach()
        endforeach()
        foreach(syntax_endian big little)
            add_test(NAME cross_syntax_patch_identity_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_patch_identity.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-patch-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_array_bounds_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/array_bounds.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-array-bounds-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_compound_updates_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/compound_updates.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-compound-updates-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls -mllsc"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_vector_lanes_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/vector_lane_updates.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-vector-lanes-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            foreach(syntax_fixture syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments meta_record_values meta_switch_values meta_lexical_context meta_namespace_imports meta_operator_values meta_continuations)
                string(REPLACE "_" "-" syntax_fixture_output "${syntax_fixture}")
                set(syntax_fixture_flags -${syntax_level} -fno-eval-calls)
                if(syntax_fixture MATCHES "^syntax_(generic|type|layout|item)_depth$")
                    list(APPEND syntax_fixture_flags -feval-step-limit=16000000)
                endif()
                if(syntax_fixture STREQUAL "syntax_recognition_depth")
                    list(APPEND syntax_fixture_flags -feval-step-limit=100000000
                        -feval-byte-limit=268435456 -feval-memory-limit=268435456)
                endif()
                list(JOIN syntax_fixture_flags " " syntax_fixture_flags_text)
                add_test(NAME cross_${syntax_fixture}_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/${syntax_fixture}.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${syntax_fixture_output}-n64-${syntax_level}-${syntax_endian}
                    -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=${syntax_fixture_flags_text}"
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
                if(syntax_fixture STREQUAL "syntax_header_nominals")
                    set_tests_properties(cross_${syntax_fixture}_n64_${syntax_level}_${syntax_endian} PROPERTIES
                        LABELS "frontend;feature.syntax;feature.generic;feature.nominal;arch.mips;profile.n64;endian.${syntax_endian}")
                endif()
            endforeach()
            add_test(NAME cross_syntax_splice_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_splice.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-splice-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_hygiene_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_hygiene.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-hygiene-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_gensym_global_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_global.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-global-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_gensym_categories_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_categories.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-categories-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_gensym_global_labels_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/gensym_global_labels.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gensym-global-labels-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_deferred_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_deferred.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-deferred-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            add_test(NAME cross_syntax_raw_n64_${syntax_level}_${syntax_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/frontend/syntax_raw_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/syntax-raw-n64-${syntax_level}-${syntax_endian}
                -DN64_ENDIAN=${syntax_endian} "-DEXTRA_FLAGS=-${syntax_level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            foreach(syntax_fixture syntax_patch_identity syntax_array_bounds syntax_compound_updates
                    syntax_vector_lanes syntax_generic_tags syntax_header_nominals syntax_unary_cast syntax_pointer_depth syntax_out_depth syntax_eval_outputs syntax_helper_constants syntax_enum_depth syntax_depth syntax_recognition_depth syntax_generic_depth syntax_type_depth syntax_layout_depth syntax_item_depth syntax_statement_depth syntax_declarations syntax_namespace_fragments
                    meta_record_values meta_switch_values meta_lexical_context meta_namespace_imports
                    meta_operator_values meta_continuations syntax_splice syntax_hygiene gensym_global gensym_categories
                    gensym_global_labels syntax_deferred syntax_raw)
                cross_syntax_test_labels(cross_${syntax_fixture}_n64_${syntax_level}_${syntax_endian}
                    mips n64 endian.${syntax_endian} check.runtime backend.native opt.${syntax_level})
                set_tests_properties(cross_${syntax_fixture}_n64_${syntax_level}_${syntax_endian}
                    PROPERTIES SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
    endforeach()
    add_test(NAME cross_meta_token_sequences
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/meta_token_sequences.x
                -DDRIVER=${CMAKE_SOURCE_DIR}/test/frontend/meta_token_sequences_driver.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/meta-token-sequences
                -P "${CMAKE_SOURCE_DIR}/test/frontend/meta_token_sequences.cmake")
    set_tests_properties(cross_meta_token_sequences PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;frontend;feature.meta")
    add_test(NAME cross_diagnostic
        COMMAND $<TARGET_FILE:cross_cc> -emit-llvm
                "${CMAKE_SOURCE_DIR}/test/frontend/diagnostic.x"
                -o "${CMAKE_CURRENT_BINARY_DIR}/diagnostic.ll")
    set_tests_properties(cross_diagnostic PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.llvm;feature.driver")
    set_tests_properties(cross_diagnostic PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cross_standalone_audit
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/basic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/standalone.s
                -P "${CMAKE_SOURCE_DIR}/test/backend/native/standalone.cmake")
    set_tests_properties(cross_standalone_audit PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;feature.standalone")
    add_test(NAME cross_runtime_parameter_copyout
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/basic.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-basic.ll
                -DENTRY=entry
                -DEXPECTED=6
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_parameter_copyout PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.semantics")
    add_test(NAME cross_runtime_namespace
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-namespace.ll
                -DENTRY=app::entry
                -DEXPECTED=41
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_namespace PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.names")
    add_test(NAME cross_runtime_computed_goto
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/labels.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runtime-labels.ll
                -DENTRY=dispatch_right
                -DEXPECTED=22
                -P "${CMAKE_SOURCE_DIR}/test/support/run_ir.cmake")
    set_tests_properties(cross_runtime_computed_goto PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.llvm;feature.labels")
    add_test(NAME cross_volatile_contract
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/volatile.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/volatile.ll
                -P "${CMAKE_SOURCE_DIR}/test/language/volatile.cmake")
    set_tests_properties(cross_volatile_contract PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.qualifiers")
    add_test(NAME cross_semantic_expansion
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/semantic_expansion.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/semantic-expansion
                -P "${CMAKE_SOURCE_DIR}/test/middle/semantic_expansion.cmake")
    set_tests_properties(cross_semantic_expansion PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.semantics")
    set_tests_properties(cross_semantic_expansion PROPERTIES TIMEOUT 15)
    add_test(NAME cross_raw_inline_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/raw_inline_errors.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-inline-errors.s
        -P "${CMAKE_SOURCE_DIR}/test/middle/raw_inline_errors.cmake")
    set_tests_properties(cross_raw_inline_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;middle;feature.raw")
    add_test(NAME cross_default_evaluation
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/default_evaluation.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/default-evaluation
                -P "${CMAKE_SOURCE_DIR}/test/middle/default_evaluation.cmake")
    set_tests_properties(cross_default_evaluation PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.evaluation")
    add_test(NAME cross_floating_evaluation COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_evaluation.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/floating_evaluation_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-evaluation
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_floating_evaluation PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.floating;feature.evaluation")
    add_test(NAME cross_raw_inline_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/raw_inline_runtime.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/raw_inline_runtime_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/raw-inline-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_raw_inline_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.raw")
    add_test(NAME cross_generic_label_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_label_value.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/generic_label_value_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-label-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_generic_label_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.generic;feature.labels")
    add_test(NAME cross_generic_label_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_label_errors.x
        -DPOSITIVE_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_label_value.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-label-errors.o
        -P "${CMAKE_SOURCE_DIR}/test/middle/generic_label_errors.cmake")
    set_tests_properties(cross_generic_label_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;middle;feature.generic;feature.labels")
    add_test(NAME cross_generic_enum_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_enum_value.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/generic_enum_value_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-enum-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_generic_enum_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.generic")
    add_test(NAME cross_generic_pointer_runtime COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_value.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-pointer-runtime
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_generic_pointer_runtime PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.generic")
    add_test(NAME cross_generic_pointer_checks COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_value.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_driver.cpp
        -DERRORS=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_errors.x
        -DMODEL=${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_models.y
        -DCUSTOM_MODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-pointer-checks
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -P "${CMAKE_SOURCE_DIR}/test/middle/generic_pointer_checks.cmake")
    set_tests_properties(cross_generic_pointer_checks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;feature.generic;profile.custom")
    add_test(NAME cross_generic_enum_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_enum_errors.x
        -DPOSITIVE_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/generic_enum_value.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-enum-errors.o
        -P "${CMAKE_SOURCE_DIR}/test/middle/generic_enum_errors.cmake")
    set_tests_properties(cross_generic_enum_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;middle;feature.generic")
    add_test(NAME cross_floating_evaluation_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/floating_evaluation_mips.x
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/floating-evaluation-mips
        -P "${CMAKE_SOURCE_DIR}/test/middle/floating_evaluation_mips.cmake")
    set_tests_properties(cross_floating_evaluation_mips PROPERTIES
        LABELS "check.compile;arch.mips;middle;feature.floating;feature.evaluation")
    add_test(NAME cross_required_constants COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/required_constants.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/required_constants_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/required-constants
        -P "${CMAKE_SOURCE_DIR}/test/middle/required_constants.cmake")
    set_tests_properties(cross_required_constants PROPERTIES
        LABELS "check.compile;arch.x86_64;middle;feature.evaluation")
    add_test(NAME cross_required_constants_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/required-constants-errors
        -P "${CMAKE_SOURCE_DIR}/test/middle/required_constants_errors.cmake")
    set_tests_properties(cross_required_constants_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;arch.mips;middle;feature.evaluation")
    add_test(NAME cross_aggregate_members COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/aggregate_members.x
        -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/aggregate_members_driver.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-members
        -P "${CMAKE_SOURCE_DIR}/test/language/semantics/runtime.cmake")
    set_tests_properties(cross_aggregate_members PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.records")
    add_test(NAME cross_aggregate_members_errors COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/aggregate-members-errors
        -P "${CMAKE_SOURCE_DIR}/test/middle/aggregate_members_errors.cmake")
    set_tests_properties(cross_aggregate_members_errors PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;middle;feature.records")
    # `out`/`inout` copy-in and copy-out: host runs per private-function ABI,
    # QEMU runs per ABI, CPU and byte order, and a frame-traffic check.
    foreach(level O0 O2)
        foreach(mode native x custom)
            add_test(NAME cross_parameter_copy_out_${mode}_${level}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out.x
                    -DDRIVER=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_driver.cpp
                    -DMODEL=${CMAKE_SOURCE_DIR}/test/model/custom.y
                    -DMODE=${mode} -DLEVEL=${level}
                    -DLLVM_TEXT=${CROSS_ENABLE_LLVM_TEXT}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parameter-copy-out-${mode}-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out.cmake")
            set_tests_properties(cross_parameter_copy_out_${mode}_${level} PROPERTIES
                LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.parameters;profile.${mode};opt.${level}")
        endforeach()
        foreach(cpu r3000 vr4300)
            foreach(endian b l)
                if(endian STREQUAL b)
                    set(endian_label big)
                else()
                    set(endian_label little)
                endif()
                set(name cross_parameter_copy_out_o32_${cpu}_${endian}_${level})
                add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out.x
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_start.s
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parameter-copy-out-o32-${cpu}-${endian}-${level}
                    -DMIPS_CPUS=${cpu} -DMIPS_ENDIANS=${endian} -DMIPS_SINGLE_CASE=ON
                    -DEXPECTED=P "-DCC_FLAGS=-${level};-fno-eval-calls"
                    -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
                set_tests_properties(${name} PROPERTIES
                    LABELS "check.runtime;arch.mips;backend.native;middle;feature.parameters;profile.o32;isa.${cpu};endian.${endian_label};opt.${level}"
                    SKIP_REGULAR_EXPRESSION "skipping MIPS")
            endforeach()
        endforeach()
        foreach(endian big little)
            set(name cross_parameter_copy_out_n64_${endian}_${level})
            add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parameter-copy-out-n64-${endian}-${level}
                -DN64_ENDIAN=${endian} "-DEXTRA_FLAGS=-${level} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${name} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;middle;feature.parameters;profile.n64;endian.${endian};opt.${level}"
                SKIP_REGULAR_EXPRESSION "skipping MIPS")
        endforeach()
    endforeach()
    add_test(NAME cross_parameter_copy_out_codegen_x86_64 COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_codegen.x
        -DARCH=x86_64 "-DTARGET_FLAGS=-target;x86_64-unknown-linux-gnu"
        "-DFLAT=copy_out_quotient;copy_out_bump;copy_out_status"
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parameter-copy-out-codegen-x86_64
        -P "${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_codegen.cmake")
    set_tests_properties(cross_parameter_copy_out_codegen_x86_64 PROPERTIES
        LABELS "check.compile;arch.x86_64;backend.native;middle;feature.parameters;opt.O2")
    add_test(NAME cross_parameter_copy_out_codegen_mips COMMAND "${CMAKE_COMMAND}"
        -DCC=$<TARGET_FILE:cross_cc>
        -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_codegen.x
        -DARCH=mips -DTARGET_FLAGS=-mprofile=vr4300-o32
        "-DFLAT=copy_out_quotient;copy_out_bump;copy_out_status"
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parameter-copy-out-codegen-mips
        -P "${CMAKE_SOURCE_DIR}/test/middle/parameter_copy_out_codegen.cmake")
    set_tests_properties(cross_parameter_copy_out_codegen_mips PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;middle;feature.parameters;profile.o32;isa.vr4300;endian.big;opt.O2")
    foreach(constants_mode O0 O3)
        add_test(NAME cross_required_constants_mips_${constants_mode} COMMAND "${CMAKE_COMMAND}"
            -DCC=$<TARGET_FILE:cross_cc>
            -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/required_constants.x
            -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/required_constants_start.s
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/required-constants-mips-${constants_mode}
            -DEXPECTED=P "-DCC_FLAGS=-${constants_mode};-fno-eval-calls"
            -P "${CMAKE_SOURCE_DIR}/test/language/semantics/mips.cmake")
        set_tests_properties(cross_required_constants_mips_${constants_mode} PROPERTIES
            LABELS "check.runtime;arch.mips;backend.native;middle;feature.evaluation;opt.${constants_mode}")
        foreach(constants_endian big little)
            add_test(NAME cross_required_constants_n64_${constants_mode}_${constants_endian} COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/required_constants.x
                -DSTARTUP=${CMAKE_SOURCE_DIR}/test/middle/required_constants_start.s
                -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/required-constants-n64-${constants_mode}-${constants_endian}
                -DN64_ENDIAN=${constants_endian} "-DEXTRA_FLAGS=-${constants_mode} -fno-eval-calls"
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(cross_required_constants_n64_${constants_mode}_${constants_endian} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;middle;feature.evaluation;endian.${constants_endian};opt.${constants_mode}")
        endforeach()
    endforeach()
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
    set_tests_properties(cross_runtime_native_default_evaluation PROPERTIES
        LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.evaluation")
    add_test(NAME cross_evaluation_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/evaluation_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evaluation-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/middle/evaluation_errors.cmake")
    set_tests_properties(cross_evaluation_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;middle;feature.evaluation")
    add_test(NAME cross_procedural_origin_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_origin_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-origin-errors.ll
                -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_origin_errors.cmake")
    set_tests_properties(cross_procedural_origin_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;frontend;feature.procedural")
    add_test(NAME cross_procedural_namespace
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_namespace.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-namespace.ll
                -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_namespace.cmake")
    set_tests_properties(cross_procedural_namespace PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.procedural;feature.names")
    add_test(NAME cross_procedural_body
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_body.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/frontend/procedural_body_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/procedural-body.ll
                -P "${CMAKE_SOURCE_DIR}/test/frontend/procedural_body.cmake")
    set_tests_properties(cross_procedural_body PROPERTIES
        LABELS "check.compile;arch.x86_64;frontend;feature.procedural")
    add_test(NAME cross_patch_value_diagnostics
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_value_errors.x
                -DCONFLICT_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_sink_conflict.x
                -DGENERIC_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_generic_conflict.x
                -DINLINE_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_inline_errors.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-value-errors
                -P "${CMAKE_SOURCE_DIR}/test/middle/patch_value_errors.cmake")
    set_tests_properties(cross_patch_value_diagnostics PROPERTIES
        LABELS "check.compile;diagnostics;arch.x86_64;middle;feature.patch")
    set_tests_properties(cross_patch_value_diagnostics PROPERTIES TIMEOUT 10)
    add_test(NAME cross_patch_structured_sinks
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_structured_sinks.x
                -DERROR_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_structured_errors.x
                -DINDEX_ERROR=${CMAKE_SOURCE_DIR}/test/middle/patch_structured_runtime_index_error.x
                -DMIPS_SOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_sink_mips.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/patch-structured-sinks
                -P "${CMAKE_SOURCE_DIR}/test/middle/patch_structured_sinks.cmake")
    set_tests_properties(cross_patch_structured_sinks PROPERTIES
        LABELS "check.compile;arch.x86_64;arch.mips;middle;feature.patch")
    foreach(level O0 O2)
        add_test(NAME cross_runtime_native_patch_structured_${level}
            COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DHOST_CXX=${CMAKE_CXX_COMPILER}
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/middle/patch_structured_sinks.x
                    -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/native-patch-structured-${level}
                    -DENTRY=patch_structured_entry
                    -DEXPECTED=1
                    -DCC_FLAGS=-${level}
                    -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_native_patch_structured_${level} PROPERTIES
            LABELS "check.runtime;arch.x86_64;backend.native;middle;feature.patch;opt.${level}")
    endforeach()
    # Do not let a new syntax/gensym registration silently disappear from the
    # feature/architecture selections. Labels describe the command's target,
    # not the host running CTest; multi-target scripts must be split first.
    get_property(cross_registered_tests DIRECTORY PROPERTY TESTS)
    foreach(cross_test IN LISTS cross_registered_tests)
        if(cross_test MATCHES "^cross_syntax_(out_depth|eval_outputs)_")
            set_property(TEST ${cross_test} APPEND PROPERTY LABELS feature.parameters feature.types)
        endif()
        get_property(cross_labels TEST ${cross_test} PROPERTY LABELS)
        if(cross_test MATCHES "^cross_(syntax_|gensym_)")
            set(cross_arch_labels ${cross_labels})
            list(FILTER cross_arch_labels INCLUDE REGEX "^arch[.]")
            list(REMOVE_DUPLICATES cross_arch_labels)
            list(LENGTH cross_arch_labels cross_arch_count)
            if(NOT "feature.syntax" IN_LIST cross_labels OR NOT cross_arch_count EQUAL 1)
                message(FATAL_ERROR "${cross_test} requires feature.syntax and exactly one architecture label")
            endif()
        endif()
        if("feature.syntax" IN_LIST cross_labels AND "profile.o32" IN_LIST cross_labels
                AND "check.runtime" IN_LIST cross_labels)
            foreach(dimension isa endian)
                set(cross_dimension_labels ${cross_labels})
                list(FILTER cross_dimension_labels INCLUDE REGEX "^${dimension}[.]")
                list(REMOVE_DUPLICATES cross_dimension_labels)
                list(LENGTH cross_dimension_labels cross_dimension_count)
                if(NOT cross_dimension_count EQUAL 1)
                    message(FATAL_ERROR "${cross_test} requires exactly one ${dimension} label; split runtime matrices")
                endif()
            endforeach()
        endif()
    endforeach()
    # test/support/mips_linker.cmake links with CROSS_TEST_LD when set.
    if(CROSS_TEST_CROSS_LD)
        set_property(TEST ${cross_registered_tests} APPEND PROPERTY
                     ENVIRONMENT "CROSS_TEST_LD=$<TARGET_FILE:cross_ld>")
    endif()
endif()
