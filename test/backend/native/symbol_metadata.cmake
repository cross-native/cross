# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC BINDING HIDDEN VISIBILITY ERRORS OUTPUT)
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
            "${suffix} symbol metadata compile failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" text)
    set(${output_var} "${text}" PARENT_SCOPE)
endfunction()

compile_assembly(binding_elf "${BINDING}" x86_64-unknown-linux-gnu binding-elf)
compile_assembly(binding_coff "${BINDING}" x86_64-w64-windows-gnu binding-coff)
compile_assembly(binding_macho "${BINDING}" x86_64-apple-darwin binding-macho)
compile_assembly(binding_mips "${BINDING}" mips64el-unknown-elf binding-mips)
foreach(pair
        "binding_elf;[.]weak cross_weak_function;[.]weak cross_weak_object"
        "binding_coff;[.]weak cross_weak_function;[.]weak cross_weak_object"
        "binding_macho;[.]weak_definition cross_weak_function;[.]weak_definition cross_weak_object"
        "binding_mips;[.]weak cross_weak_function;[.]weak cross_weak_object")
    list(GET pair 0 variable)
    list(GET pair 1 function_pattern)
    list(GET pair 2 object_pattern)
    if(NOT "${${variable}}" MATCHES "${function_pattern}" OR
       NOT "${${variable}}" MATCHES "${object_pattern}")
        message(FATAL_ERROR
            "${variable} lacks weak symbol directives\n${${variable}}")
    endif()
endforeach()

compile_assembly(visibility_elf "${VISIBILITY}" x86_64-unknown-linux-gnu visibility-elf)
compile_assembly(visibility_mips "${VISIBILITY}" mips64el-unknown-elf visibility-mips)
foreach(variable visibility_elf visibility_mips)
    foreach(kind hidden protected internal)
        if(NOT "${${variable}}" MATCHES
           "[.]${kind} cross_${kind}_function" OR
           NOT "${${variable}}" MATCHES "[.]${kind} cross_${kind}_object")
            message(FATAL_ERROR
                "${variable} lacks ${kind} symbol directives\n${${variable}}")
        endif()
    endforeach()
endforeach()

compile_assembly(hidden_macho "${HIDDEN}" x86_64-apple-darwin hidden-macho)
foreach(pattern
        "[.]private_extern cross_hidden_function"
        "[.]private_extern cross_hidden_object")
    if(NOT hidden_macho MATCHES "${pattern}")
        message(FATAL_ERROR "Mach-O lacks hidden symbol directive ${pattern}")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               x86_64-apple-darwin mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -c -O2 -target "${target}" "${BINDING}"
                -o "${OUTPUT}-binding-${target}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${target} weak object compile failed\n${stdout}\n${stderr}")
    endif()
endforeach()
execute_process(
    COMMAND "${CC}" -c -O2 -target x86_64-unknown-linux-gnu "${VISIBILITY}"
            -o "${OUTPUT}-visibility-elf.o"
    RESULT_VARIABLE visibility_object_status
    OUTPUT_VARIABLE visibility_object_stdout ERROR_VARIABLE visibility_object_stderr)
if(NOT visibility_object_status EQUAL 0)
    message(FATAL_ERROR
        "ELF visibility object failed\n${visibility_object_stdout}\n${visibility_object_stderr}")
endif()
execute_process(
    COMMAND "${LLVM_READOBJ}" --symbols "${OUTPUT}-binding-x86_64-unknown-linux-gnu.o"
    RESULT_VARIABLE read_status OUTPUT_VARIABLE binding_symbols
    ERROR_VARIABLE read_stderr)
if(NOT read_status EQUAL 0 OR NOT binding_symbols MATCHES "Binding: Weak")
    message(FATAL_ERROR "ELF weak binding missing\n${binding_symbols}\n${read_stderr}")
endif()
execute_process(
    COMMAND "${LLVM_READOBJ}" --symbols "${OUTPUT}-visibility-elf.o"
    RESULT_VARIABLE read_status OUTPUT_VARIABLE visibility_symbols
    ERROR_VARIABLE read_stderr)
foreach(kind HIDDEN PROTECTED INTERNAL)
    if(NOT visibility_symbols MATCHES "STV_${kind}")
        message(FATAL_ERROR
            "ELF ${kind} visibility missing\n${visibility_symbols}\n${read_stderr}")
    endif()
endforeach()

foreach(case coff-hidden macho-protected)
    if(case STREQUAL "coff-hidden")
        set(target x86_64-w64-windows-gnu)
        set(source "${HIDDEN}")
        set(pattern "hidden visibility is not representable in COFF")
    else()
        set(target x86_64-apple-darwin)
        set(source "${VISIBILITY}")
        set(pattern "protected visibility is only representable in ELF")
    endif()
    execute_process(
        COMMAND "${CC}" -S -target "${target}" "${source}"
                -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "${case} did not diagnose '${pattern}'\n${stdout}\n${stderr}")
    endif()
endforeach()

foreach(source_name binding visibility)
    string(TOUPPER "${source_name}" source_variable)
    execute_process(
        COMMAND "${CC}" -emit-llvm -O2 "${${source_variable}}"
                -o "${OUTPUT}-${source_name}.ll"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "LLVM ${source_name} metadata failed\n${stdout}\n${stderr}")
    endif()
endforeach()
file(READ "${OUTPUT}-binding.ll" binding_llvm)
if(NOT binding_llvm MATCHES "define weak [^\n]*@\"cross_weak_function\"" OR
   NOT binding_llvm MATCHES "@\"cross_weak_object\" = weak global")
    message(FATAL_ERROR "LLVM weak metadata missing\n${binding_llvm}")
endif()
file(READ "${OUTPUT}-visibility.ll" visibility_llvm)
foreach(pattern
        "define hidden [^\n]*@\"cross_hidden_function\""
        "define protected [^\n]*@\"cross_protected_function\""
        "define hidden [^\n]*@\"cross_internal_function\""
        "@\"cross_hidden_object\" = hidden global"
        "@\"cross_protected_object\" = protected global"
        "@\"cross_internal_object\" = hidden global")
    if(NOT visibility_llvm MATCHES "${pattern}")
        message(FATAL_ERROR "LLVM visibility metadata lacks ${pattern}\n${visibility_llvm}")
    endif()
endforeach()
find_program(LLVM_AS NAMES llvm-as REQUIRED)
foreach(source_name binding visibility)
    execute_process(
        COMMAND "${LLVM_AS}" "${OUTPUT}-${source_name}.ll"
                -o "${OUTPUT}-${source_name}.bc"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "LLVM rejected ${source_name} metadata\n${stdout}\n${stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-gimple -O2 "${VISIBILITY}"
            -o "${OUTPUT}-visibility.gimple.c"
    RESULT_VARIABLE gimple_status OUTPUT_VARIABLE gimple_stdout
    ERROR_VARIABLE gimple_stderr)
if(NOT gimple_status EQUAL 0)
    message(FATAL_ERROR
        "GIMPLE visibility metadata failed\n${gimple_stdout}\n${gimple_stderr}")
endif()
file(READ "${OUTPUT}-visibility.gimple.c" gimple)
foreach(kind hidden protected internal)
    if(NOT gimple MATCHES "visibility\\(\"${kind}\"\\)")
        message(FATAL_ERROR "GIMPLE visibility metadata lacks ${kind}")
    endif()
endforeach()
find_program(HOST_GCC NAMES gcc)
if(HOST_GCC)
    execute_process(
        COMMAND "${HOST_GCC}" -c -O2 -fgimple "${OUTPUT}-visibility.gimple.c"
                -o "${OUTPUT}-visibility-gimple.o"
        RESULT_VARIABLE gcc_status OUTPUT_VARIABLE gcc_stdout
        ERROR_VARIABLE gcc_stderr)
    if(NOT gcc_status EQUAL 0)
        message(FATAL_ERROR
            "GCC rejected visibility metadata\n${gcc_stdout}\n${gcc_stderr}")
    endif()
endif()

execute_process(
    COMMAND "${CC}" -S "${ERRORS}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid symbol metadata compiled")
endif()
foreach(pattern
        "weak does not take arguments"
        "weak requires an external definition"
        "weak requires global linkage"
        "visibility requires one visibility-kind string"
        "visibility must be default, hidden, protected, or internal"
        "visibility requires global linkage"
        "conflicting visibility attributes")
    if(NOT error_stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "symbol metadata diagnostic lacks '${pattern}'\n${error_stderr}")
    endif()
endforeach()
