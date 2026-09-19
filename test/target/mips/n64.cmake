# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
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
            "${label} MIPS n64 compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

set(assembly "${OUTPUT}.be.s")
set(large_assembly "${OUTPUT}.large.s")
set(big_object "${OUTPUT}.be.o")
set(little_object "${OUTPUT}.le.o")

run_cc(n64-assembly -S -O2 -mprofile=mips64-n64 "${SOURCE}" -o "${assembly}")
run_cc(n64-large-assembly -S -O2 -mprofile=mips64-n64 -mcmodel=large
       "${SOURCE}" -o "${large_assembly}")

# The default sym32 model never needs the upper relocation halves; the large
# model materializes every symbol address from all four and calls through
# a register.
file(READ "${large_assembly}" large_text)
foreach(pattern
        "%highest\\("
        "%higher\\("
        "[\t ]dsll[\t ][$][0-9]+,[$][0-9]+,16"
        "[\t ]jalr[\t ]+[$]1")
    if(NOT large_text MATCHES "${pattern}")
        message(FATAL_ERROR
            "n64 large-model assembly is missing '${pattern}'\n${large_text}")
    endif()
endforeach()
run_cc(n64-object -c -O2 -mprofile=mips64-n64 "${SOURCE}" -o "${big_object}")
run_cc(n64el-object -c -O2 -mprofile=mips64el-n64 "${SOURCE}"
       -o "${little_object}")

file(READ "${assembly}" text)
if(text MATCHES "%highest\\(")
    message(FATAL_ERROR "n64 sym32 assembly must not use %highest\n${text}")
endif()

# GPRs must be spelled numerically: an n64 assembler reads $t0-$t3 as the
# architectural registers 12-15 and $8-$11 as $a4-$a7.
foreach(pattern
        "\.set mips64"
        "\.frame[\t ]+[$]29,"
        "\.frame[\t ][^\n]*,[$]31"
        "[\t ]daddiu[\t ][$]29,[$]29,-"
        "[\t ]sd[\t ][$]31,"
        "[\t ]ld[\t ][$]31,"
        "[\t ]daddu[\t ]"
        "[\t ]dsll[\t ]"
        "[\t ]ld[\t ][$][0-9]+,[0-9]+[(][$][0-9]+[)]"
        "[\t ]sd[\t ][$][0-9]+,[0-9]+[(][$][0-9]+[)]"
        "[\t ]lui[\t ][$][0-9]+,%hi[(]n64_object[)]"
        "[\t ]daddiu[\t ][$][0-9]+,[$][0-9]+,%lo[(]n64_object[)]"
        "[\t ]ld[\t ][$][0-9]+,0[(][$]29[)]"
        "[\t ]ld[\t ][$][0-9]+,8[(][$]29[)]"
        "[\t ]mov\.d[\t ][^\n]*[$]f13"
        "[\t ]mov\.s[\t ][^\n]*[$]f14"
        "\n\t\.quad n64_object\n"
        "\\.quad \\.Lcross\\.patch\\.value\\.[0-9]+\\.end-8")
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR
            "MIPS n64 assembly is missing '${pattern}'\n${text}")
    endif()
endforeach()
foreach(forbidden
        "[$](zero|at|v[01]|a[0-3]|t[0-9]|s[0-7]|k[01]|gp|sp|fp|ra)[^a-z0-9_]"
        "[\t ]addiu[\t ][$][0-9]+,[$]29,"
        "[\t ]sw[\t ][$][0-9]+,[0-9]+[(][$]29[)]"
        "[\t ]lw[\t ][$][0-9]+,[0-9]+[(][$]29[)]")
    if(text MATCHES "${forbidden}")
        message(FATAL_ERROR
            "MIPS n64 assembly still uses '${forbidden}'\n${text}")
    endif()
endforeach()

foreach(object "${big_object}" "${little_object}")
    execute_process(
        COMMAND "${LLVM_READOBJ}" --file-headers "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE header ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot inspect '${object}'\n${error}")
    endif()
    foreach(pattern "Format: elf64-mips" "Class: 64-bit" "EF_MIPS_ARCH_64")
        if(NOT header MATCHES "${pattern}")
            message(FATAL_ERROR
                "n64 object '${object}' is missing '${pattern}'\n${header}")
        endif()
    endforeach()
    # EF_MIPS_ABI2 marks n32; n64 carries no ABI tag in e_flags at all.
    foreach(forbidden "EF_MIPS_ABI2" "EF_MIPS_ABI_O32" "EF_MIPS_ABI_EABI")
        if(header MATCHES "${forbidden}")
            message(FATAL_ERROR
                "n64 object '${object}' is tagged '${forbidden}'\n${header}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers "${little_object}"
    OUTPUT_VARIABLE le_header)
if(NOT le_header MATCHES "DataEncoding: LittleEndian")
    message(FATAL_ERROR "mips64el-n64 object is not little-endian\n${le_header}")
endif()

execute_process(
    COMMAND "${LLVM_READELF}" -A "${big_object}"
    RESULT_VARIABLE abi_status OUTPUT_VARIABLE abi_flags ERROR_VARIABLE abi_error)
if(NOT abi_status EQUAL 0 OR
   NOT abi_flags MATCHES "ISA: MIPS64" OR
   NOT abi_flags MATCHES "GPR size: 64" OR
   NOT abi_flags MATCHES "CPR1 size: 64" OR
   NOT abi_flags MATCHES "FP ABI: Hard float")
    message(FATAL_ERROR
        "n64 object has incorrect MIPS ABI flags\n${abi_flags}\n${abi_error}")
endif()

# The ABI's address model and the triple's ELF class must agree.
function(expect_error label expected)
    execute_process(
        COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

expect_error(n64-on-mips32 "needs a mips64/mips64el target triple"
    -c -O2 -target mips-unknown-elf -mabi=n64 "${SOURCE}"
    -o "${OUTPUT}.mismatch.o")
expect_error(o32-on-mips64 "cannot be selected for a mips64 target triple"
    -c -O2 -target mips64-unknown-elf -mabi=o32 "${SOURCE}"
    -o "${OUTPUT}.mismatch.o")
expect_error(n64-without-mips3 "needs the 64-bit MIPS III register file"
    -c -O2 -mprofile=mips64-n64 -march=mips1 "${SOURCE}"
    -o "${OUTPUT}.mismatch.o")
expect_error(n64-single-float "defined on a 64-bit FPU"
    -c -O2 -mprofile=mips64-n64 -msingle-float "${SOURCE}"
    -o "${OUTPUT}.mismatch.o")
