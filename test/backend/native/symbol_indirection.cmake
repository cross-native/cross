# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC ALIAS WEAK_ALIAS WEAKREF WEAKREF_DECLS ERRORS HARNESS
                 HOST_CXX OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly output_var source target suffix)
    execute_process(
        COMMAND "${CC}" -S -O2 -target "${target}" "${source}"
                -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${suffix} symbol-indirection compile failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" text)
    set(${output_var} "${text}" PARENT_SCOPE)
endfunction()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               x86_64-apple-darwin mips64el-unknown-elf)
    string(REPLACE "-" "_" suffix "${target}")
    # Mach-O spells link names with a leading underscore.
    set(prefix "")
    if(target STREQUAL "x86_64-apple-darwin")
        set(prefix "_")
    endif()
    compile_assembly(alias_${suffix} "${ALIAS}" "${target}" alias-${suffix})
    foreach(pair
            "cross_alias_function;cross_alias_target_function"
            "cross_alias_object;cross_alias_target_object")
        list(GET pair 0 alias_name)
        list(GET pair 1 target_name)
        if(NOT "${alias_${suffix}}" MATCHES
           "[.]set ${prefix}${alias_name},${prefix}${target_name}")
            message(FATAL_ERROR
                "${target} lacks alias ${alias_name}\n${alias_${suffix}}")
        endif()
    endforeach()
    compile_assembly(weakref_${suffix} "${WEAKREF}" "${target}"
                     weakref-${suffix})
    foreach(name cross_optional_function cross_optional_object)
        if(target STREQUAL "x86_64-apple-darwin")
            set(pattern "[.]weak_reference _${name}")
        else()
            set(pattern "[.]weak ${name}")
        endif()
        if(NOT "${weakref_${suffix}}" MATCHES "${pattern}")
            message(FATAL_ERROR
                "${target} lacks weak reference ${name}\n${weakref_${suffix}}")
        endif()
    endforeach()
endforeach()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               mips64el-unknown-elf)
    string(REPLACE "-" "_" suffix "${target}")
    compile_assembly(weak_alias_${suffix} "${WEAK_ALIAS}" "${target}"
                     weak-alias-${suffix})
    foreach(name cross_weak_alias_function cross_weak_alias_object)
        if(NOT "${weak_alias_${suffix}}" MATCHES "[.]weak ${name}")
            message(FATAL_ERROR
                "${target} lacks weak alias ${name}\n${weak_alias_${suffix}}")
        endif()
    endforeach()
endforeach()
execute_process(
    COMMAND "${CC}" -S -target x86_64-apple-darwin "${WEAK_ALIAS}"
            -o "${OUTPUT}-weak-alias-macho.s"
    RESULT_VARIABLE macho_weak_status OUTPUT_VARIABLE macho_weak_stdout
    ERROR_VARIABLE macho_weak_stderr)
if(macho_weak_status EQUAL 0 OR
   NOT macho_weak_stderr MATCHES
       "weak aliases are not representable in Mach-O")
    message(FATAL_ERROR
        "Mach-O weak alias was not diagnosed\n${macho_weak_stdout}\n${macho_weak_stderr}")
endif()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
foreach(source_name alias weakref)
    string(TOUPPER "${source_name}" source_variable)
    foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
                   x86_64-apple-darwin mips64el-unknown-elf)
        execute_process(
            COMMAND "${CC}" -c -O2 -target "${target}"
                    "${${source_variable}}"
                    -o "${OUTPUT}-${source_name}-${target}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "${target} ${source_name} object failed\n${stdout}\n${stderr}")
        endif()
    endforeach()
endforeach()
execute_process(
    COMMAND "${CC}" -c -O2 -target x86_64-unknown-linux-gnu "${WEAK_ALIAS}"
            -o "${OUTPUT}-weak-alias-elf.o"
    RESULT_VARIABLE weak_alias_status OUTPUT_VARIABLE weak_alias_stdout
    ERROR_VARIABLE weak_alias_stderr)
if(NOT weak_alias_status EQUAL 0)
    message(FATAL_ERROR
        "weak alias ELF object failed\n${weak_alias_stdout}\n${weak_alias_stderr}")
endif()

execute_process(
    COMMAND "${LLVM_READOBJ}" --symbols --sections
            "${OUTPUT}-alias-x86_64-unknown-linux-gnu.o"
    RESULT_VARIABLE read_status OUTPUT_VARIABLE alias_symbols
    ERROR_VARIABLE read_stderr)
foreach(name cross_alias_function cross_alias_object)
    if(NOT alias_symbols MATCHES
       "Name: ${name}[^}]*Section: [.]((text)|(data))[.]cross_alias_target")
        message(FATAL_ERROR
            "ELF alias symbol ${name} is not defined with its target\n${alias_symbols}\n${read_stderr}")
    endif()
endforeach()
if(NOT alias_symbols MATCHES "SHF_GNU_RETAIN")
    message(FATAL_ERROR "alias retention did not reach target sections")
endif()

execute_process(
    COMMAND "${LLVM_READOBJ}" --symbols "${OUTPUT}-weak-alias-elf.o"
    RESULT_VARIABLE read_status OUTPUT_VARIABLE weak_alias_symbols
    ERROR_VARIABLE read_stderr)
foreach(name cross_weak_alias_function cross_weak_alias_object)
    if(NOT weak_alias_symbols MATCHES "Name: ${name}[^}]*Binding: Weak")
        message(FATAL_ERROR
            "ELF weak alias ${name} lacks weak binding\n${weak_alias_symbols}\n${read_stderr}")
    endif()
endforeach()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               x86_64-apple-darwin)
    execute_process(
        COMMAND "${LLVM_READOBJ}" --symbols
                "${OUTPUT}-weakref-${target}.o"
        RESULT_VARIABLE read_status OUTPUT_VARIABLE weakref_symbols
        ERROR_VARIABLE read_stderr)
    if(NOT read_status EQUAL 0)
        message(FATAL_ERROR "could not inspect ${target} weak references")
    endif()
    if(target STREQUAL "x86_64-unknown-linux-gnu")
        set(weak_pattern "Binding: Weak")
    elseif(target STREQUAL "x86_64-w64-windows-gnu")
        set(weak_pattern "StorageClass: WeakExternal")
    else()
        set(weak_pattern "WeakRef")
    endif()
    if(NOT weakref_symbols MATCHES "${weak_pattern}")
        message(FATAL_ERROR
            "${target} object lacks weak undefined symbols\n${weakref_symbols}\n${read_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${HOST_CXX}" -c "${HARNESS}" -o "${OUTPUT}-harness.o"
    RESULT_VARIABLE harness_status OUTPUT_VARIABLE harness_stdout
    ERROR_VARIABLE harness_stderr)
execute_process(
    COMMAND "${HOST_CXX}" "${OUTPUT}-harness.o"
            "${OUTPUT}-alias-x86_64-w64-windows-gnu.o"
            -o "${OUTPUT}-runtime.exe"
    RESULT_VARIABLE link_status OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr)
if(NOT harness_status EQUAL 0 OR NOT link_status EQUAL 0)
    message(FATAL_ERROR
        "alias runtime link failed\n${harness_stdout}\n${harness_stderr}\n${link_stdout}\n${link_stderr}")
endif()
execute_process(COMMAND "${OUTPUT}-runtime.exe" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "alias runtime failed with ${run_status}")
endif()

foreach(source_name alias weakref)
    string(TOUPPER "${source_name}" source_variable)
    execute_process(
        COMMAND "${CC}" -emit-llvm -O2 "${${source_variable}}"
                -o "${OUTPUT}-${source_name}.ll"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "LLVM ${source_name} emission failed\n${stdout}\n${stderr}")
    endif()
endforeach()
file(READ "${OUTPUT}-alias.ll" alias_llvm)
foreach(pattern
        "@\"cross_alias_function\" = alias i32 \\(\\), ptr @\"cross_alias_target_function\""
        "@\"cross_alias_object\" = alias i32, ptr @\"cross_alias_target_object\"")
    if(NOT alias_llvm MATCHES "${pattern}")
        message(FATAL_ERROR "LLVM alias metadata lacks ${pattern}\n${alias_llvm}")
    endif()
endforeach()
file(READ "${OUTPUT}-weakref.ll" weakref_llvm)
foreach(pattern
        "declare extern_weak i32 @\"cross_optional_function\"\\(\\)"
        "@\"cross_optional_object\" = extern_weak global i32")
    if(NOT weakref_llvm MATCHES "${pattern}")
        message(FATAL_ERROR
            "LLVM weak-reference metadata lacks ${pattern}\n${weakref_llvm}")
    endif()
endforeach()
find_program(LLVM_AS NAMES llvm-as REQUIRED)
foreach(source_name alias weakref)
    execute_process(
        COMMAND "${LLVM_AS}" "${OUTPUT}-${source_name}.ll"
                -o "${OUTPUT}-${source_name}.bc"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "LLVM rejected ${source_name} metadata\n${stdout}\n${stderr}")
    endif()
endforeach()

foreach(source_name alias weakref-declarations)
    if(source_name STREQUAL "alias")
        set(source "${ALIAS}")
    else()
        set(source "${WEAKREF_DECLS}")
    endif()
    execute_process(
        COMMAND "${CC}" -emit-gimple -O2 "${source}"
                -o "${OUTPUT}-${source_name}.gimple.c"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "GIMPLE ${source_name} emission failed\n${stdout}\n${stderr}")
    endif()
endforeach()
file(READ "${OUTPUT}-alias.gimple.c" alias_gimple)
if(NOT alias_gimple MATCHES "alias\\(\"cross_alias_target_function\"\\)" OR
   NOT alias_gimple MATCHES "alias\\(\"cross_alias_target_object\"\\)")
    message(FATAL_ERROR "GIMPLE alias metadata missing\n${alias_gimple}")
endif()
file(READ "${OUTPUT}-weakref-declarations.gimple.c" weakref_gimple)
if(NOT weakref_gimple MATCHES "cross_optional_function[^\n]*weak" OR
   NOT weakref_gimple MATCHES "cross_optional_object[^\n]*weak")
    message(FATAL_ERROR "GIMPLE weak-reference metadata missing\n${weakref_gimple}")
endif()
find_program(HOST_GCC NAMES gcc)
if(HOST_GCC)
    foreach(source_name alias weakref-declarations)
        execute_process(
            COMMAND "${HOST_GCC}" -c -O2 -fgimple
                    "${OUTPUT}-${source_name}.gimple.c"
                    -o "${OUTPUT}-${source_name}-gimple.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "GCC rejected ${source_name} metadata\n${stdout}\n${stderr}")
        endif()
    endforeach()
endif()

execute_process(
    COMMAND "${CC}" -S "${ERRORS}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid symbol indirections compiled")
endif()
foreach(pattern
        "alias requires one link-name string"
        "alias requires one nonempty link-name string"
        "weakref requires one link-name string"
        "weakref requires one nonempty link-name string"
        "function alias target 'missing_function' is not a definition"
        "function alias target 'incompatible_function_target' has an incompatible type or ABI"
        "object alias target 'missing_object' is not a definition"
        "object alias target 'incompatible_object_target' has an incompatible type, alignment, or storage contract"
        "alias function cannot have a body"
        "alias object cannot have an initializer"
        "weakref function cannot have a body"
        "weakref object cannot have an initializer"
        "weakref already supplies weak binding"
        "weakref cannot have non-default visibility"
        "weakref target 'already_defined_weakref_target' is already declared"
        "alias and weakref cannot be combined"
        "alias requires global linkage"
        "conflicting alias attributes")
    if(NOT error_stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "symbol-indirection diagnostic lacks '${pattern}'\n${error_stderr}")
    endif()
endforeach()
