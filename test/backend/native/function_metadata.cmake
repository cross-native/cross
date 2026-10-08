# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERRORS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly name target)
    execute_process(
        COMMAND "${CC}" -S -O2 -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} metadata compile failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${name}.s" ${name})
    set(${name} "${${name}}" PARENT_SCOPE)
endfunction()

compile_assembly(elf x86_64-unknown-linux-gnu)
foreach(pattern
        "[.]section \"[.]text[.]hot[.]metadata_hot\",\"ax\",@progbits"
        "[.]section \"[.]text[.]unlikely[.]metadata_cold\",\"axR\",@progbits")
    if(NOT elf MATCHES "${pattern}")
        message(FATAL_ERROR "ELF function metadata lacks ${pattern}\n${elf}")
    endif()
endforeach()

compile_assembly(coff x86_64-w64-windows-gnu)
foreach(pattern
        "[.]section \"[.]text[$]hot[.]metadata_hot\",\"xr\""
        "[.]section \"[.]text[$]cold[.]metadata_cold\",\"xr\""
        "-include:metadata_cold")
    if(NOT coff MATCHES "${pattern}")
        message(FATAL_ERROR "COFF function metadata lacks ${pattern}\n${coff}")
    endif()
endforeach()

compile_assembly(mips mips64el-unknown-elf)
foreach(pattern
        "[.]text[.]hot[.]metadata_hot"
        "[.]text[.]unlikely[.]metadata_cold"
        "\"axR\"")
    if(NOT mips MATCHES "${pattern}")
        message(FATAL_ERROR "MIPS function metadata lacks ${pattern}\n${mips}")
    endif()
endforeach()

compile_assembly(macho x86_64-apple-darwin)
foreach(pattern
        "[.]section __TEXT,__text_hot,regular,pure_instructions"
        "[.]section __TEXT,__text_cold,regular,pure_instructions"
        "[.]no_dead_strip metadata_cold")
    if(NOT macho MATCHES "${pattern}")
        message(FATAL_ERROR "Mach-O function metadata lacks ${pattern}\n${macho}")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               x86_64-apple-darwin mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -c -O2 -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${target}.o"
        RESULT_VARIABLE object_status OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr)
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "${target} function metadata object failed\n${object_stdout}\n${object_stderr}")
    endif()
endforeach()
execute_process(
    COMMAND "${LLVM_READOBJ}" --sections
            "${OUTPUT}-x86_64-unknown-linux-gnu.o"
    RESULT_VARIABLE read_status OUTPUT_VARIABLE elf_sections
    ERROR_VARIABLE read_stderr)
if(NOT read_status EQUAL 0 OR
   NOT elf_sections MATCHES
       "Name: [.]text[.]unlikely[.]metadata_cold[^}]*SHF_GNU_RETAIN")
    message(FATAL_ERROR "ELF function retain flag missing\n${elf_sections}\n${read_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm -O2 "${SOURCE}" -o "${OUTPUT}.ll"
    RESULT_VARIABLE llvm_status OUTPUT_VARIABLE llvm_stdout
    ERROR_VARIABLE llvm_stderr)
if(NOT llvm_status EQUAL 0)
    message(FATAL_ERROR "LLVM metadata compile failed\n${llvm_stdout}\n${llvm_stderr}")
endif()
file(READ "${OUTPUT}.ll" llvm)
foreach(pattern
        "define [^\n]*@\"metadata_hot\"\\([^\n]* hot"
        "define [^\n]*@\"metadata_cold\"\\([^\n]* cold"
        "@llvm[.]compiler[.]used = appending global \\[1 x ptr\\] \\[ptr @\"metadata_hot\"\\]"
        "@llvm[.]used = appending global \\[1 x ptr\\] \\[ptr @\"metadata_cold\"\\]")
    if(NOT llvm MATCHES "${pattern}")
        message(FATAL_ERROR "LLVM function metadata lacks ${pattern}\n${llvm}")
    endif()
endforeach()
find_program(LLVM_AS NAMES llvm-as REQUIRED)
execute_process(
    COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
    RESULT_VARIABLE llvm_as_status OUTPUT_VARIABLE llvm_as_stdout
    ERROR_VARIABLE llvm_as_stderr)
if(NOT llvm_as_status EQUAL 0)
    message(FATAL_ERROR "LLVM rejected function metadata\n${llvm_as_stdout}\n${llvm_as_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-gimple -O2 "${SOURCE}"
            -o "${OUTPUT}.gimple.c"
    RESULT_VARIABLE gimple_status OUTPUT_VARIABLE gimple_stdout
    ERROR_VARIABLE gimple_stderr)
if(NOT gimple_status EQUAL 0)
    message(FATAL_ERROR "GIMPLE metadata compile failed\n${gimple_stdout}\n${gimple_stderr}")
endif()
file(READ "${OUTPUT}.gimple.c" gimple)
foreach(pattern "hot" "cold" "used" "retain" "no_stack_protector"
                "no_sanitize\\(\"address\"\\)"
                "no_sanitize\\(\"undefined\"\\)")
    if(NOT gimple MATCHES "${pattern}")
        message(FATAL_ERROR "GIMPLE function metadata lacks ${pattern}")
    endif()
endforeach()
find_program(HOST_GCC NAMES gcc)
if(HOST_GCC)
    execute_process(
        COMMAND "${HOST_GCC}" -c -O2 -fgimple "${OUTPUT}.gimple.c"
                -o "${OUTPUT}-gimple.o"
        RESULT_VARIABLE gcc_status OUTPUT_VARIABLE gcc_stdout
        ERROR_VARIABLE gcc_stderr)
    if(NOT gcc_status EQUAL 0)
        message(FATAL_ERROR "GCC rejected function metadata\n${gcc_stdout}\n${gcc_stderr}")
    endif()
endif()

# Compile independently: source attribute errors may precede HIR metadata
# checks, and one early error must not suppress coverage of the other cases.
set(error_case 0)
foreach(pattern
        "a function cannot be both hot and cold"
        "used requires a function definition"
        "retain requires a function definition"
        "hot does not take arguments"
        "no_stack_protector does not take arguments"
        "no_sanitize requires one instrumentation-name string"
        "no_sanitize requires one nonempty instrumentation-name string"
        "no_sanitize requires one nonempty instrumentation-name string"
        "no_sanitize requires one instrumentation-name string")
    execute_process(
        COMMAND "${CC}" -S "-DFUNCTION_METADATA_ERROR=${error_case}" "${ERRORS}"
                -o "${OUTPUT}-errors-${error_case}.s"
        RESULT_VARIABLE error_status OUTPUT_VARIABLE error_stdout ERROR_VARIABLE error_stderr)
    if(NOT error_status EQUAL 1 OR NOT error_stderr MATCHES "${pattern}" OR
       NOT error_stderr MATCHES "function_metadata_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "function metadata diagnostic lacks '${pattern}'\n${error_stderr}")
    endif()
    math(EXPR error_case "${error_case} + 1")
endforeach()
