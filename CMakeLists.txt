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
    src/common/diagnostic.cpp
    src/common/floating_bits.cpp
    src/common/floating_semantics.cpp
    src/common/options.cpp
    src/common/source.cpp
)
cross_component(cross_common)
target_compile_definitions(cross_common PRIVATE
    CROSS_DEFAULT_TARGET="${cross_default_target}")

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
    src/middle/mir_pass.cpp
    src/middle/mir_transform.cpp
    src/middle/shrink_wrap.cpp
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
    src/driver/stack.cpp
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
    foreach(variant O0 O2 O3 O2_noalloc)
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
        set(generic_attribute_flags
            "--model=${CMAKE_SOURCE_DIR}/test/model/custom.y"
            -O2 -fno-register-allocation -fno-eval-calls)
        if(profile STREQUAL odd)
            list(APPEND generic_attribute_flags -mabi=odd_abi)
        endif()
        add_test(NAME cross_runtime_generic_attribute_noalloc_${profile}
            COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DHOST_CXX=${CMAKE_CXX_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/language/generic_angle_custom_abi.x
                -DRUNNER=${CMAKE_SOURCE_DIR}/test/support/native_entry.cpp
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-attribute-noalloc-${profile}
                -DENTRY=generic_angle_custom_abi_entry -DEXPECTED=1
                "-DCC_FLAGS=${generic_attribute_flags}"
                -P "${CMAKE_SOURCE_DIR}/test/support/run_native.cmake")
        set_tests_properties(cross_runtime_generic_attribute_noalloc_${profile} PROPERTIES
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
        -DCC=$<TARGET_FILE:cross_cc> -DHOST_CXX=${CMAKE_CXX_COMPILER}
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
    set_tests_properties(cross_mips_allegrex_runtime PROPERTIES
        LABELS "check.runtime;arch.mips;backend.native;feature.codegen")
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
    add_test(NAME cross_mips_cross_n64
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DCALLEE=${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64_callee.x
                -DCALLER=${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64_caller.x
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-cross-n64
                -P "${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64.cmake")
    set_tests_properties(cross_mips_cross_n64 PROPERTIES
        LABELS "check.compile;arch.mips;backend.native;feature.abi;profile.n64")
    # Separately compiled caller and callee; the callee spells -mabi=cross.
    foreach(cross_n64_endian big little)
        foreach(cross_n64_level O0 O2)
            set(cross_n64_test cross_mips_cross_n64_runtime_${cross_n64_endian}_${cross_n64_level})
            add_test(NAME ${cross_n64_test}
                COMMAND "${CMAKE_COMMAND}"
                    -DCC=$<TARGET_FILE:cross_cc>
                    -DSOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64_caller.x
                    -DCALLEE_SOURCE=${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64_callee.x
                    -DCALLEE_FLAGS=-mabi=cross -DMIPS64_ABI=default
                    -DSTARTUP=${CMAKE_SOURCE_DIR}/test/target/mips/cross_n64_runtime.s
                    -DLINKER=${CMAKE_SOURCE_DIR}/test/target/mips/bare-n64.ld
                    -DN64_ENDIAN=${cross_n64_endian} -DEXTRA_FLAGS=-${cross_n64_level}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mips-cross-n64-runtime-${cross_n64_endian}-${cross_n64_level}
                    -P "${CMAKE_SOURCE_DIR}/test/target/mips/n64_runtime.cmake")
            set_tests_properties(${cross_n64_test} PROPERTIES
                LABELS "check.runtime;arch.mips;backend.native;feature.abi;profile.n64;endian.${cross_n64_endian};opt.${cross_n64_level}")
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
    foreach(abi o32 cross32)
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
    add_test(NAME cross_mangling_dsl
        COMMAND "${CMAKE_COMMAND}"
                -DCC=$<TARGET_FILE:cross_cc>
                -DSOURCE=${CMAKE_SOURCE_DIR}/test/model/mangling_symbols.x
                -DMODEL=${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.y
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mangling-dsl
                -P "${CMAKE_SOURCE_DIR}/test/model/mangling_dsl.cmake")
    set_tests_properties(cross_mangling_dsl PROPERTIES
        LABELS "check.compile;arch.x86_64;feature.names;feature.model;feature.generic")
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
    endforeach()
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
                -DEXPECTED=576
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
                -DEXPECTED=576
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
    foreach(abi_cpu cross32:r3000 cross32:vr4300 cross64:vr4300)
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
    add_test(NAME cross_forced_includes
        COMMAND "${CMAKE_COMMAND}"
                -DCPP=$<TARGET_FILE:cross_cpp>
                -DCC=$<TARGET_FILE:cross_cc>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/forced-includes
                -P "${CMAKE_SOURCE_DIR}/test/frontend/forced_includes.cmake")
    set_tests_properties(cross_forced_includes PROPERTIES
        LABELS "frontend;feature.preprocessor;arch.x86_64;check.compile;diagnostics")
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
        foreach(mode native cross custom)
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
endif()
