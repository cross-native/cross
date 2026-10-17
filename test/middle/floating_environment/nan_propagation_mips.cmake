# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs SOURCE, compiled for MIPS32R2 with the IEEE 754-2008 NaN encoding, on
# QEMU's P5600, whose FCSR.NAN2008 is set, in both byte orders. The startup
# prints P when test_entry returns zero.

foreach(required CC SOURCE STARTUP LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_MC NAMES llvm-mc)
include("${CMAKE_CURRENT_LIST_DIR}/../../support/mips_linker.cmake")
find_program(QEMU_B NAMES qemu-system-mips)
find_program(QEMU_L NAMES qemu-system-mipsel)
if(NOT LLVM_MC OR NOT LLD)
    message(STATUS "skipping MIPS NaN propagation test: llvm-mc and ld.lld are required")
    return()
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

foreach(endian b l)
    if(endian STREQUAL "b")
        set(arch mips)
        set(qemu "${QEMU_B}")
    else()
        set(arch mipsel)
        set(qemu "${QEMU_L}")
    endif()
    set(out "${OUTPUT}-${endian}")
    run_checked(compile "${CC}" -target ${arch}-unknown-elf -march=mips32r2 -mnan2008
        -mabi=o32 -O2 ${CC_FLAGS} -c "${SOURCE}" -o "${out}.o")
    run_checked(startup "${LLVM_MC}" --filetype=obj --triple=${arch}-unknown-elf
        --mcpu=mips32r2 --mattr=+noabicalls,+nan2008,-fp64 "${STARTUP}" -o "${out}.start.o")
    run_checked(link "${LLD}" -m elf32${endian}tsmip -T "${LINKER}"
        "${out}.start.o" "${out}.o" -o "${out}.elf")
    if(NOT qemu)
        message(STATUS "skipping MIPS ${endian}: QEMU unavailable")
        continue()
    endif()
    execute_process(COMMAND "${qemu}" -M malta -cpu P5600 -m 64M -bios none
        -kernel "${out}.elf" -display none -serial none -serial none -serial stdio
        -monitor none -no-reboot -semihosting RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
    string(STRIP "${stdout}" stdout)
    if(NOT status EQUAL 0 OR NOT stdout STREQUAL "P")
        message(FATAL_ERROR "MIPS ${endian}: result was not P\n${stdout}\n${stderr}")
    endif()
endforeach()
