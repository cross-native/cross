# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MEMORY_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
find_program(LLVM_READELF NAMES llvm-readelf REQUIRED)

function(run_cc label)
    execute_process(
        COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${label} MIPS compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

set(big_assembly "${OUTPUT}.be.s")
set(fix_assembly "${OUTPUT}.fix4300.s")
set(big_object "${OUTPUT}.be.o")
set(little_object "${OUTPUT}.le.o")
set(memory_assembly "${OUTPUT}.memory.s")
set(memory_object "${OUTPUT}.memory.o")
set(memory_little_object "${OUTPUT}.memory.le.o")

run_cc(big-assembly -S -O2 -mprofile=vr4300-o32 "${SOURCE}"
       -o "${big_assembly}")
run_cc(fix4300 -S -O2 -mprofile=vr4300-o32 -mfix4300 "${SOURCE}"
       -o "${fix_assembly}")
run_cc(big-object -c -O2 -mprofile=vr4300-o32 "${SOURCE}"
       -o "${big_object}")
run_cc(little-object -c -O2 -target mipsel-unknown-elf -mabi=o32
       -march=vr4300 "${SOURCE}" -o "${little_object}")
run_cc(memory-assembly -S -O2 -mprofile=vr4300-o32
       "${MEMORY_SOURCE}" -o "${memory_assembly}")
run_cc(memory-object -c -O2 -mprofile=vr4300-o32
       "${MEMORY_SOURCE}" -o "${memory_object}")
run_cc(memory-little-object -c -O2 -target mipsel-unknown-elf
       -mabi=o32 -march=vr4300 "${MEMORY_SOURCE}"
       -o "${memory_little_object}")

file(READ "${big_assembly}" assembly)
foreach(pattern
        ".set mips3"
        "\\.frame[\t ]+\\$sp,"
        "[\t ]daddu[\t ]"
        "[\t ]mul\\.d[\t ]"
        "[\t ]mov\\.d[\t ][^\n]*[$]f12"
        "[\t ]mov\\.d[\t ][^\n]*[$]f14"
        "[\t ]c\\.lt\\.s[\t ]"
        "[\t ]c\\.lt\\.d[\t ]"
        "[\t ]sub\\.s[\t ]"
        "[\t ]sub\\.d[\t ]"
        "[\t ]trunc\\.w\\.s[\t ]"
        "[\t ]trunc\\.w\\.d[\t ]"
        "[\t ]jal[\t ]mips_add64"
        "[\t ]jal[\t ]mips_fma_shape")
    if(NOT assembly MATCHES "${pattern}")
        message(FATAL_ERROR
            "VR4300/o32 assembly is missing '${pattern}'\n${assembly}")
    endif()
endforeach()
if(assembly MATCHES
   "[\t ](trunc\\.l\\.[sd]|cvt\\.[sd]\\.l)[\t ]")
    message(FATAL_ERROR
        "VR4300 FP32 conversion selected a reserved long-FPR instruction\n${assembly}")
endif()

file(READ "${fix_assembly}" fixed)
if(NOT fixed MATCHES "[\t ]mul\\.d[^\n]*\n[\t ]add\\.d" OR
   fixed MATCHES "[\t ]mul\\.d[^\n]*\n[\t ]nop")
    message(FATAL_ERROR
        "-mfix4300 did not use safe work between FP multiplies\n${fixed}")
endif()

file(READ "${memory_assembly}" memory)
foreach(pattern
        "[\t ]ll[\t ]"
        "[\t ]sc[\t ]"
        "[\t ]sync"
        "\\.text\\.cross\\.patch\\."
        "\\.Lcross\\.patch\\.value\\.[0-9]+\\.end:"
        "\\.long \\.Lcross\\.patch\\.value\\.[0-9]+\\.end-4"
        "\\.long \\.Lcross\\.patch\\.value\\.[0-9]+\\.end-1"
        "\\.long \\.Lcross\\.patch\\.value\\.[0-9]+\\.end-2"
        "%hi\\(mips_global\\)"
        "%hi\\(mips_atomic\\)")
    if(NOT memory MATCHES "${pattern}")
        message(FATAL_ERROR
            "MIPS memory/atomic assembly is missing '${pattern}'\n${memory}")
    endif()
endforeach()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${big_object}"
    RESULT_VARIABLE be_status OUTPUT_VARIABLE be_header ERROR_VARIABLE be_error)
execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${little_object}"
    RESULT_VARIABLE le_status OUTPUT_VARIABLE le_header ERROR_VARIABLE le_error)
if(NOT be_status EQUAL 0 OR NOT le_status EQUAL 0)
    message(FATAL_ERROR "cannot inspect MIPS objects\n${be_error}\n${le_error}")
endif()
foreach(pattern "Format: elf32-mips" "DataEncoding: BigEndian"
                "EF_MIPS_ABI_O32" "EF_MIPS_ARCH_3")
    if(NOT be_header MATCHES "${pattern}")
        message(FATAL_ERROR "big-endian object is missing '${pattern}'\n${be_header}")
    endif()
endforeach()
if(be_header MATCHES "EF_MIPS_CPIC")
    message(FATAL_ERROR "non-PIC VR4300 object was incorrectly marked CPIC")
endif()
foreach(pattern "Format: elf32-mips" "DataEncoding: LittleEndian"
                "EF_MIPS_ABI_O32" "EF_MIPS_ARCH_3")
    if(NOT le_header MATCHES "${pattern}")
        message(FATAL_ERROR "little-endian object is missing '${pattern}'\n${le_header}")
    endif()
endforeach()

execute_process(
    COMMAND "${LLVM_READELF}" -A "${big_object}"
    RESULT_VARIABLE abi_status OUTPUT_VARIABLE abi_flags ERROR_VARIABLE abi_error)
if(NOT abi_status EQUAL 0 OR
   NOT abi_flags MATCHES "ISA: MIPS3" OR
   NOT abi_flags MATCHES "GPR size: 64" OR
   NOT abi_flags MATCHES "CPR1 size: 32" OR
   NOT abi_flags MATCHES "FP ABI: Hard float")
    message(FATAL_ERROR
        "VR4300 object has incorrect MIPS ABI flags\n${abi_flags}\n${abi_error}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -target mips-unknown-elf -mabi=o32
            -march=vr4300 -msoft-float "${SOURCE}"
            -o "${OUTPUT}.soft-float.s"
    RESULT_VARIABLE soft_status OUTPUT_VARIABLE soft_stdout
    ERROR_VARIABLE soft_stderr)
if(soft_status EQUAL 0 OR
   NOT soft_stderr MATCHES "standalone Cross will not insert a software-float runtime call")
    message(FATAL_ERROR
        "MIPS soft-float diagnostics are missing\n${soft_stdout}\n${soft_stderr}")
endif()
