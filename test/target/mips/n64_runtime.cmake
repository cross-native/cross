# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE STARTUP LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_MC NAMES llvm-mc)
find_program(LLD NAMES ld.lld)
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
run_checked(cross-functions "${CC}" -c -O2 -mprofile=${n64_arch}-n64 ${extra_flags}
            "${SOURCE}" -o "${functions}")
run_checked(startup "${LLVM_MC}" --filetype=obj
            --triple=${n64_arch}-unknown-elf --mcpu=mips64
            --mattr=+noabicalls "${STARTUP}" -o "${start}")
run_checked(link-elf64 "${LLD}" -m ${n64_link_emulation} -T "${LINKER}"
            "${start}" "${functions}" -o "${image}")

execute_process(
    COMMAND "${QEMU}" -M malta -cpu MIPS64R2-generic -m 64M -bios none
            -kernel "${image}" -display none
            -serial none -serial none -serial stdio -monitor none
            -no-reboot -semihosting
    RESULT_VARIABLE qemu_status
    OUTPUT_VARIABLE qemu_stdout
    ERROR_VARIABLE qemu_stderr
    TIMEOUT 30)
if(NOT qemu_status EQUAL 0 OR NOT qemu_stdout MATCHES "(^|[\r\n])P[\r\n]")
    message(FATAL_ERROR
        "MIPS64R2 n64 execution did not report success (status ${qemu_status})\n${qemu_stdout}\n${qemu_stderr}")
endif()
