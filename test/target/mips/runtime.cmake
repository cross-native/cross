# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MEMORY_SOURCE STARTUP LINKER WRAPPER_TEMPLATE
                 WRAPPER_LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLD NAMES ld.lld)
find_program(QEMU NAMES qemu-system-mips64)
if(NOT LLVM_MC OR NOT LLVM_OBJCOPY OR NOT LLD OR NOT QEMU)
    message(STATUS
        "skipping MIPS runtime test: llvm-mc, llvm-objcopy, ld.lld, and qemu-system-mips64 are required")
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
set(memory "${OUTPUT}.memory.o")
set(start "${OUTPUT}.start.o")
set(elf32 "${OUTPUT}.elf32")
set(image "${OUTPUT}.bin")
set(wrapper_source "${OUTPUT}.wrapper.s")
set(wrapper_object "${OUTPUT}.wrapper.o")
set(elf64 "${OUTPUT}.elf64")

run_checked(cross-functions "${CC}" -c -O2 -mprofile=vr4300-o32
            "${SOURCE}" -o "${functions}")
run_checked(cross-memory "${CC}" -c -O2 -mprofile=vr4300-o32
            "${MEMORY_SOURCE}" -o "${memory}")
run_checked(startup "${LLVM_MC}" --filetype=obj
            --triple=mips-unknown-elf --mcpu=mips3
            --mattr=+noabicalls "${STARTUP}" -o "${start}")
run_checked(link-elf32 "${LLD}" -m elf32btsmip -T "${LINKER}"
            "${start}" "${functions}" "${memory}" -o "${elf32}")
run_checked(flatten-image "${LLVM_OBJCOPY}" -O binary "${elf32}" "${image}")

# Malta's 64-bit firmware preserves a 64-bit ELF entry address. The wrapper
# only transports the already-relocated ELF32 image into sign-extended KSEG0;
# every instruction under test remains the exact Cross/llvm-mc o32 output.
file(TO_CMAKE_PATH "${image}" IMAGE)
configure_file("${WRAPPER_TEMPLATE}" "${wrapper_source}" @ONLY)
run_checked(wrapper "${LLVM_MC}" --filetype=obj
            --triple=mips64-unknown-elf --mcpu=mips3
            "${wrapper_source}" -o "${wrapper_object}")
run_checked(link-wrapper "${LLD}" -m elf64btsmip -T "${WRAPPER_LINKER}"
            "${wrapper_object}" -o "${elf64}")

execute_process(
    COMMAND "${QEMU}" -M malta -cpu R4000 -m 64M -bios none
            -kernel "${elf64}" -display none
            -serial none -serial none -serial stdio -monitor none
            -no-reboot -semihosting
    RESULT_VARIABLE qemu_status
    OUTPUT_VARIABLE qemu_stdout
    ERROR_VARIABLE qemu_stderr
    TIMEOUT 15)
if(NOT qemu_status EQUAL 0 OR NOT qemu_stdout MATCHES "(^|[\r\n])P[\r\n]")
    message(FATAL_ERROR
        "R4000 execution did not report success (status ${qemu_status})\n${qemu_stdout}\n${qemu_stderr}")
endif()
