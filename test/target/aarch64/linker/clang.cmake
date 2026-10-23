# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Freestanding C compiled by Clang (CLANG) for AArch64 at -O0 and -O2, as
# static code linked at QEMU virt's load address and as position-
# independent code (-fPIE) linked with -pie: with qemu-system-aarch64 the
# images run (the position-independent one from a flat binary at two load
# addresses, relocating itself) and exit with status 0.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
if(NOT CLANG OR NOT QEMU OR NOT LLVM_OBJCOPY)
    message(STATUS "skipping: Clang, qemu-system-aarch64, and llvm-objcopy are required")
    return()
endif()
assemble(cross "${sources}/clang_start.s" "${OUTPUT}/start.o")
foreach(level O0 O2)
    foreach(model static pie)
        set(base "${OUTPUT}/${model}-${level}")
        set(flags "")
        set(link "--defsym;BASE=0x40080000")
        if(model STREQUAL "pie")
            set(flags -fPIE)
            set(link "-pie;--defsym;BASE=0")
        endif()
        run_checked("clang -${level} ${model}" "${CLANG}" -target aarch64-none-elf -ffreestanding -fno-builtin
                    -${level} ${flags} -c "${sources}/clang.c" -o "${base}.o")
        run_checked("ld ${model} -${level}" "${LD}" ${link} -T "${sources}/clang.ld" "${OUTPUT}/start.o" "${base}.o"
                    -o "${base}.elf")
        if(model STREQUAL "static")
            qemu_exits_zero("the static -${level} image" -kernel "${base}.elf")
            continue()
        endif()
        run_checked("llvm-objcopy" "${LLVM_OBJCOPY}" -O binary "${base}.elf" "${base}.bin")
        foreach(address 0x40300000 0x41000000)
            qemu_exits_zero("the position-independent -${level} image at ${address}"
                            -device "loader,file=${base}.bin,addr=${address},cpu-num=0,force-raw=on")
        endforeach()
    endforeach()
endforeach()
