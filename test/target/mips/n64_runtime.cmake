# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE STARTUP LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_MC NAMES llvm-mc)
include("${CMAKE_CURRENT_LIST_DIR}/../../support/mips_linker.cmake")
if(N64_ENDIAN STREQUAL "little")
    set(n64_arch mips64el)
    set(n64_link_emulation elf64ltsmip)
    find_program(QEMU_N64_L NAMES qemu-system-mips64el)
    set(QEMU "${QEMU_N64_L}")
else()
    set(n64_arch mips64)
    set(n64_link_emulation elf64btsmip)
    find_program(QEMU_N64_B NAMES qemu-system-mips64)
    set(QEMU "${QEMU_N64_B}")
endif()
if(NOT LLVM_MC OR NOT LLD OR NOT QEMU)
    message(STATUS
        "skipping MIPS n64 runtime test: llvm-mc, ld.lld, and qemu-system-mips64 are required")
    return()
endif()
# An optional C unit, compiled by GNU GCC with GCC_FLAGS, checks that its
# objects and Cross objects link and run together.
if(DEFINED GCC_SOURCE AND NOT "${GCC_SOURCE}" STREQUAL "")
    find_program(GNU_GCC NAMES mips64-elf-gcc)
    if(NOT GNU_GCC)
        message(STATUS "skipping MIPS n64 runtime test: mips64-elf-gcc is required")
        return()
    endif()
endif()

function(run_checked label)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

set(functions "${OUTPUT}.functions.o")
set(start "${OUTPUT}.start.o")
set(image "${OUTPUT}.elf64")

# n64 objects are already ELF64 with a sign-extended KSEG0 entry address, so
# unlike the o32 test this needs no ELF32-to-ELF64 wrapper.
if(DEFINED EXTRA_FLAGS AND NOT "${EXTRA_FLAGS}" STREQUAL "")
    separate_arguments(extra_flags NATIVE_COMMAND "${EXTRA_FLAGS}")
else()
    set(extra_flags "")
endif()
# Hand-written startups call the compiled entry points as n64 functions, so
# the compilation selects n64 unless MIPS64_ABI names another ABI; "default"
# keeps the profile's own default.
if(NOT DEFINED MIPS64_ABI OR "${MIPS64_ABI}" STREQUAL "")
    set(abi_flags -mabi=n64)
elseif(MIPS64_ABI STREQUAL "default")
    set(abi_flags "")
else()
    set(abi_flags "-mabi=${MIPS64_ABI}")
endif()
run_checked(cross-functions "${CC}" -c -O2 -mprofile=${n64_arch}-n64 ${abi_flags}
            ${extra_flags} "${SOURCE}" -o "${functions}")
set(objects "${functions}")
# An optional second unit, compiled by its own cc invocation.
if(DEFINED CALLEE_SOURCE AND NOT "${CALLEE_SOURCE}" STREQUAL "")
    if(DEFINED CALLEE_FLAGS AND NOT "${CALLEE_FLAGS}" STREQUAL "")
        separate_arguments(callee_flags NATIVE_COMMAND "${CALLEE_FLAGS}")
    else()
        set(callee_flags "")
    endif()
    set(callee "${OUTPUT}.callee.o")
    run_checked(cross-callee "${CC}" -c -O2 -mprofile=${n64_arch}-n64 ${abi_flags}
                ${extra_flags} ${callee_flags} "${CALLEE_SOURCE}" -o "${callee}")
    list(APPEND objects "${callee}")
endif()
if(DEFINED GCC_SOURCE AND NOT "${GCC_SOURCE}" STREQUAL "")
    set(gcc_flags "")
    if(GCC_FLAGS)
        separate_arguments(gcc_flags NATIVE_COMMAND "${GCC_FLAGS}")
    endif()
    if(N64_ENDIAN STREQUAL "little")
        set(gcc_endian -EL)
    else()
        set(gcc_endian -EB)
    endif()
    run_checked(gcc "${GNU_GCC}" ${gcc_endian} -march=mips64 -mabi=64
                -mno-abicalls -fno-pic -ffreestanding -O2 ${gcc_flags} -c
                "${GCC_SOURCE}" -o "${OUTPUT}.gcc.o")
    list(APPEND objects "${OUTPUT}.gcc.o")
endif()
# Startups shared with 32-bit address tests enable Status.FR, which these
# ABIs need, under CROSS_MIPS_FR64. Release 6 objects link only with
# Release 6 code, and Release 6 has its own SDBBP for the semihosting exit.
set(startup_source "${STARTUP}")
set(startup_cpu mips64)
if(MIPS_RELEASE6)
    file(READ "${STARTUP}" startup_text)
    string(REGEX REPLACE "\\.set([ \t]+)mips64([ \t\r\n])" ".set\\1mips64r6\\2" startup_text
           "${startup_text}")
    string(REGEX REPLACE "\\.word([ \t]+)0x7000007f" ".word\\10x0000004e" startup_text
           "${startup_text}")
    set(startup_source "${OUTPUT}.start.s")
    file(WRITE "${startup_source}" "${startup_text}")
    set(startup_cpu mips64r6)
endif()
run_checked(startup "${LLVM_MC}" --filetype=obj
            --triple=${n64_arch}-unknown-elf --mcpu=${startup_cpu}
            --mattr=+noabicalls --defsym=CROSS_MIPS_FR64=1 "${startup_source}"
            -o "${start}")
run_checked(link-elf64 "${LLD}" -m ${n64_link_emulation} -T "${LINKER}"
            "${start}" ${objects} -o "${image}")

# N64_QEMU_CPU selects another emulated CPU, such as an R4000 for MIPS III
# code.
if(NOT DEFINED N64_QEMU_CPU OR "${N64_QEMU_CPU}" STREQUAL "")
    set(N64_QEMU_CPU MIPS64R2-generic)
endif()
execute_process(
    COMMAND "${QEMU}" -M malta -cpu ${N64_QEMU_CPU} -m 64M -bios none
            -kernel "${image}" -display none
            -serial none -serial none -serial stdio -monitor none
            -no-reboot -semihosting
    RESULT_VARIABLE qemu_status
    OUTPUT_VARIABLE qemu_stdout
    ERROR_VARIABLE qemu_stderr
    TIMEOUT 30)
if(NOT qemu_status EQUAL 0 OR NOT qemu_stdout MATCHES "(^|[\r\n])P[\r\n]")
    message(FATAL_ERROR
        "${N64_QEMU_CPU} n64 execution did not report success (status ${qemu_status})\n${qemu_stdout}\n${qemu_stderr}")
endif()
