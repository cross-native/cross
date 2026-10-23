# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The bare-metal program of the assembler tests (SOURCE, laid out by
# SCRIPT), linked by ld from the objects of Cross as, llvm-mc, and GNU as
# (where installed): the images agree and equal ld.lld --no-relax's image,
# and with qemu-system-aarch64 they run on the virt machine and exit
# through semihosting with status 0.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
foreach(required SOURCE SCRIPT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
set(assemblers cross mc)
if(GNU_AS)
    list(APPEND assemblers gnu)
endif()
foreach(assembler IN LISTS assemblers)
    # GNU ld writes the object's name into the image's symbols.
    file(MAKE_DIRECTORY "${OUTPUT}/${assembler}")
    set(object "${OUTPUT}/${assembler}/program.o")
    assemble(${assembler} "${SOURCE}" "${object}")
    run_checked("ld of the ${assembler} object" "${LD}" -T "${SCRIPT}" "${object}" -o "${OUTPUT}/${assembler}/ld.elf")
    if(NOT assembler STREQUAL "cross")
        same_images("the images of the cross and ${assembler} objects" "${OUTPUT}/cross/ld.elf"
                    "${OUTPUT}/${assembler}/ld.elf")
    endif()
endforeach()
set(image "${OUTPUT}/cross/ld.elf")
if(LLD)
    run_checked("ld.lld" "${LLD}" --no-relax -T "${SCRIPT}" "${OUTPUT}/cross/program.o" -o "${OUTPUT}/lld.elf")
    same_images("the images of ld and ld.lld" "${image}" "${OUTPUT}/lld.elf")
else()
    message(STATUS "ld.lld not found")
endif()
if(NOT QEMU)
    message(STATUS "qemu-system-aarch64 not found; the images were not run")
    return()
endif()
foreach(assembler IN LISTS assemblers)
    qemu_exits_zero("the image of the ${assembler} object" -kernel "${OUTPUT}/${assembler}/ld.elf")
endforeach()
