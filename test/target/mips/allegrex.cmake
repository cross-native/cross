# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MIPS1_SOURCE F64_SOURCE OUTPUT)
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
        ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${label} MIPS compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

set(eabi_assembly "${OUTPUT}.eabi.s")
set(baseline_assembly "${OUTPUT}.baseline.s")
set(eabi_object "${OUTPUT}.eabi.o")
set(o32_object "${OUTPUT}.o32.o")
set(mips1_assembly "${OUTPUT}.mips1.s")
set(mips1_object "${OUTPUT}.mips1.o")
set(mips2_object "${OUTPUT}.mips2.o")

run_cc(allegrex-assembly -S -O2 -mprofile=psp-allegrex "${SOURCE}"
       -o "${eabi_assembly}")
run_cc(allegrex-baseline-assembly -S -O2 -mprofile=psp-allegrex
       -mno-rotate -mno-cond-move "${SOURCE}" -o "${baseline_assembly}")
run_cc(allegrex-eabi-object -c -O2
       -target mipsallegrexel-sony-psp-elf "${SOURCE}" -o "${eabi_object}")
run_cc(allegrex-o32-object -c -O2 -mprofile=allegrex-o32 "${SOURCE}"
       -o "${o32_object}")
run_cc(mips1-assembly -S -O1 -mprofile=r3000-o32 "${MIPS1_SOURCE}"
       -o "${mips1_assembly}")
run_cc(mips1-object -c -O1 -mprofile=r3000-o32 "${MIPS1_SOURCE}"
       -o "${mips1_object}")
run_cc(mips2-object -c -O1 -mprofile=r6000-eabi "${MIPS1_SOURCE}"
       -o "${mips2_object}")

file(READ "${eabi_assembly}" assembly)
foreach(pattern
        "\\.section \\.mdebug\\.eabi32"
        "\\.section \\.gcc_compiled_long32"
        "\\.set mips2"
        "\\.word[	 ]+0x01285046"
        "\\.word[	 ]+0x0128500b"
        "swc1[	 ]+[$]f12"
        "swc1[	 ]+[$]f13")
    if(NOT assembly MATCHES "${pattern}")
        message(FATAL_ERROR
            "Allegrex/EABI32 assembly is missing '${pattern}'\n${assembly}")
    endif()
endforeach()
if(assembly MATCHES "[	 ](daddu|dsubu|ld|sd|ldc1|sdc1)[	 ]")
    message(FATAL_ERROR
        "Allegrex assembly contains a MIPS-III/64-bit instruction\n${assembly}")
endif()

file(READ "${baseline_assembly}" baseline)
if(baseline MATCHES "\\.word[	 ]+0x(01285046|0128500b)" OR
   NOT baseline MATCHES "[	 ]srlv[	 ]" OR
   NOT baseline MATCHES "[	 ]beq[	 ]")
    message(FATAL_ERROR
        "Allegrex extension disable switches did not affect selection\n${baseline}")
endif()
if(NOT assembly MATCHES "c\\.[a-z]+\\.s[^\r\n]*[\r\n]+[\t ]nop[\r\n]+[\t ]bc1")
    message(FATAL_ERROR
        "Allegrex floating compare hazard was not separated\n${assembly}")
endif()

file(READ "${mips1_assembly}" mips1)
if(NOT mips1 MATCHES "\\.set mips1" OR
   NOT mips1 MATCHES "[\t ]lw[^\r\n]*[\r\n]+[\t ]nop")
    message(FATAL_ERROR
        "MIPS-I assembly is missing its ISA or load-delay marker\n${mips1}")
endif()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${eabi_object}"
    RESULT_VARIABLE eabi_status OUTPUT_VARIABLE eabi_header
    ERROR_VARIABLE eabi_error)
execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${o32_object}"
    RESULT_VARIABLE o32_status OUTPUT_VARIABLE o32_header
    ERROR_VARIABLE o32_error)
if(NOT eabi_status EQUAL 0 OR NOT o32_status EQUAL 0)
    message(FATAL_ERROR
        "cannot inspect Allegrex objects\n${eabi_error}\n${o32_error}")
endif()
foreach(pattern "Format: elf32-mips" "DataEncoding: LittleEndian"
                "EF_MIPS_ABI_EABI32" "EF_MIPS_ARCH_2")
    if(NOT eabi_header MATCHES "${pattern}")
        message(FATAL_ERROR
            "Allegrex EABI32 object is missing '${pattern}'\n${eabi_header}")
    endif()
endforeach()
foreach(pattern "Format: elf32-mips" "DataEncoding: LittleEndian"
                "EF_MIPS_ABI_O32" "EF_MIPS_ARCH_2")
    if(NOT o32_header MATCHES "${pattern}")
        message(FATAL_ERROR
            "Allegrex o32 object is missing '${pattern}'\n${o32_header}")
    endif()
endforeach()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${mips1_object}"
    RESULT_VARIABLE mips1_status OUTPUT_VARIABLE mips1_header
    ERROR_VARIABLE mips1_error)
execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${mips2_object}"
    RESULT_VARIABLE mips2_status OUTPUT_VARIABLE mips2_header
    ERROR_VARIABLE mips2_error)
if(NOT mips1_status EQUAL 0 OR NOT mips2_status EQUAL 0)
    message(FATAL_ERROR
        "cannot inspect early-MIPS objects\n${mips1_error}\n${mips2_error}")
endif()
foreach(pattern "DataEncoding: BigEndian" "EF_MIPS_ABI_O32")
    if(NOT mips1_header MATCHES "${pattern}")
        message(FATAL_ERROR
            "R3000/o32 object is missing '${pattern}'\n${mips1_header}")
    endif()
endforeach()
if(mips1_header MATCHES "EF_MIPS_ARCH_[2-9]")
    message(FATAL_ERROR
        "R3000 object was tagged with a later MIPS ISA\n${mips1_header}")
endif()
foreach(pattern "DataEncoding: BigEndian" "EF_MIPS_ABI_EABI32"
                "EF_MIPS_ARCH_2")
    if(NOT mips2_header MATCHES "${pattern}")
        message(FATAL_ERROR
            "R6000/EABI32 object is missing '${pattern}'\n${mips2_header}")
    endif()
endforeach()

execute_process(
    COMMAND "${LLVM_READELF}" -A "${eabi_object}"
    RESULT_VARIABLE abi_status OUTPUT_VARIABLE abi_flags
    ERROR_VARIABLE abi_error)
execute_process(
    COMMAND "${LLVM_READELF}" -A "${o32_object}"
    RESULT_VARIABLE o32_abi_status OUTPUT_VARIABLE o32_abi_flags
    ERROR_VARIABLE o32_abi_error)
if(NOT abi_status EQUAL 0 OR
   NOT abi_flags MATCHES "ISA: MIPS2" OR
   NOT abi_flags MATCHES "GPR size: 32" OR
   NOT abi_flags MATCHES "CPR1 size: 32" OR
   NOT abi_flags MATCHES "FP ABI: Hard float [(]single precision[)]")
    message(FATAL_ERROR
        "Allegrex object has incorrect ABI flags\n${abi_flags}\n${abi_error}")
endif()
if(NOT o32_abi_status EQUAL 0 OR
   NOT o32_abi_flags MATCHES "FP ABI: Hard float [(]single precision[)]")
    message(FATAL_ERROR
        "Allegrex o32 object lost single-float metadata\n${o32_abi_flags}\n${o32_abi_error}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -mprofile=psp-allegrex "${F64_SOURCE}"
            -o "${OUTPUT}.f64.s"
    RESULT_VARIABLE f64_status OUTPUT_VARIABLE f64_stdout
    ERROR_VARIABLE f64_stderr)
if(f64_status EQUAL 0 OR
   NOT f64_stderr MATCHES "single-precision-only FPU")
    message(FATAL_ERROR
        "Allegrex f64 diagnostic is missing\n${f64_stdout}\n${f64_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -mprofile=psp-allegrex -mno-single-float
            "${F64_SOURCE}" -o "${OUTPUT}.invalid-cpu.s"
    RESULT_VARIABLE cpu_status OUTPUT_VARIABLE cpu_stdout
    ERROR_VARIABLE cpu_stderr)
if(cpu_status EQUAL 0 OR
   NOT cpu_stderr MATCHES "CPU baseline cannot be disabled")
    message(FATAL_ERROR
        "Allegrex CPU-baseline validation is missing\n${cpu_stdout}\n${cpu_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -mprofile=psp-allegrex -mvfpu "${SOURCE}"
            -o "${OUTPUT}.vfpu.s"
    RESULT_VARIABLE vfpu_status OUTPUT_VARIABLE vfpu_stdout
    ERROR_VARIABLE vfpu_stderr)
if(vfpu_status EQUAL 0 OR
   NOT vfpu_stderr MATCHES "overlapping scalar/vector/matrix register lowering")
    message(FATAL_ERROR
        "Allegrex VFPU boundary diagnostic is missing\n${vfpu_stdout}\n${vfpu_stderr}")
endif()
