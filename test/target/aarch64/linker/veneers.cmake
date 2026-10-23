# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Range-extension veneers: a program whose code a script places 256 MiB
# apart (adrp veneers) or more than 4 GiB apart (long veneers) calls and
# tail-calls across in both directions. Every B and BL reaches the address
# it reaches in the images of GNU ld and ld.lld; the veneers have GNU ld's
# names, forms, targets, and placement; and with qemu-system-aarch64 the
# images run and exit with status 0.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
assemble(cross "${sources}/veneers.s" "${OUTPUT}/veneers.o")
foreach(layout veneers veneers_long)
    set(script "${sources}/${layout}.ld")
    set(base "${OUTPUT}/${layout}")
    run_checked("ld ${layout}" "${LD}" -T "${script}" "${OUTPUT}/veneers.o" -o "${base}.elf")
    run_checked("branches of ld's ${layout} image" "${PYTHON}" "${images}" branches "${base}.elf"
                near_begin near_end far_begin far_end)
    set(cross_branches "${run_stdout}")
    run_checked("veneers of ld's ${layout} image" "${PYTHON}" "${images}" veneers "${base}.elf")
    set(cross_veneers "${run_stdout}")
    set(form adrp)
    if(layout STREQUAL "veneers_long")
        set(form long)
    endif()
    expect("${layout} veneers" "${cross_veneers}" "__far_one_veneer ${form} " "__far_two_veneer ${form} "
           "__.far_veneer ${form} " "__far_calls_back_veneer ${form} " "__far_tail_back_veneer ${form} "
           "__.text_veneer ${form} " "__near_five_veneer ${form} " "stubs .text " "stubs .far ")
    string(REGEX MATCHALL "__far_one_veneer" shared "${cross_veneers}")
    list(LENGTH shared count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "${layout}: the calls of far_one in one group do not share a veneer\n${cross_veneers}")
    endif()
    foreach(linker LLD GNU_LD)
        if(NOT ${linker})
            message(STATUS "${linker} not found")
            continue()
        endif()
        run_checked("${linker} ${layout}" "${${linker}}" -T "${script}" "${OUTPUT}/veneers.o" -o "${base}-${linker}.elf")
        run_checked("branches of the ${linker} ${layout} image" "${PYTHON}" "${images}" branches
                    "${base}-${linker}.elf" near_begin near_end far_begin far_end)
        if(NOT run_stdout STREQUAL cross_branches)
            message(FATAL_ERROR "${layout}: branches reach other addresses than with ${linker}\n"
                                "ld:\n${cross_branches}${linker}:\n${run_stdout}")
        endif()
        if(linker STREQUAL "GNU_LD")
            run_checked("veneers of GNU ld's ${layout} image" "${PYTHON}" "${images}" veneers "${base}-${linker}.elf")
            if(NOT run_stdout STREQUAL cross_veneers)
                message(FATAL_ERROR "${layout}: veneers differ from GNU ld's\nld:\n${cross_veneers}"
                                    "GNU ld:\n${run_stdout}")
            endif()
        endif()
    endforeach()
    if(QEMU)
        qemu_exits_zero("${layout}" -m 8G -kernel "${base}.elf")
    endif()
endforeach()
if(NOT QEMU)
    message(STATUS "qemu-system-aarch64 not found; the images were not run")
endif()
