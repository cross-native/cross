# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles SOURCE, one input or a list of them, for AArch64 with OPT_LEVEL
# and FLAGS, links it with start.s through the Cross as and ld, and runs the
# image on QEMU's virt machine: the test passes when the program exits with
# status 0 (see start.s for the statuses of failed checks).

foreach(required CC AS LD SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
if(NOT DEFINED OPT_LEVEL OR "${OPT_LEVEL}" STREQUAL "")
    set(OPT_LEVEL -O2)
endif()
if(NOT DEFINED TARGET OR "${TARGET}" STREQUAL "")
    set(TARGET aarch64-none-elf)
endif()
find_program(QEMU NAMES qemu-system-aarch64)
if(NOT QEMU)
    message(STATUS "skipping: qemu-system-aarch64 is required")
    return()
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out
                    ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${out}\n${err}")
    endif()
endfunction()

separate_arguments(flags NATIVE_COMMAND "${FLAGS}")
file(MAKE_DIRECTORY "${OUTPUT}")
run_checked(compile "${CC}" -target ${TARGET} ${OPT_LEVEL} ${flags} -c "${SOURCE}"
            -o "${OUTPUT}/program.o")
run_checked(startup "${AS}" -target aarch64-none-elf "${CMAKE_CURRENT_LIST_DIR}/start.s"
            -o "${OUTPUT}/start.o")
set(objects "${OUTPUT}/start.o" "${OUTPUT}/program.o")
# C_SOURCE, freestanding C of the test's own, compiled by CLANG.
if(DEFINED C_SOURCE AND NOT "${C_SOURCE}" STREQUAL "")
    if(NOT DEFINED CLANG OR NOT EXISTS "${CLANG}")
        message(STATUS "skipping: Clang is required")
        return()
    endif()
    # The virt machine runs with the MMU off, where unaligned accesses fault.
    run_checked(clang "${CLANG}" -target aarch64-none-elf -ffreestanding -fno-builtin
                -mno-unaligned-access -O2 -c "${C_SOURCE}" -o "${OUTPUT}/c.o")
    list(APPEND objects "${OUTPUT}/c.o")
endif()
# EXTRA_ASSEMBLY, an assembly file of the test's own.
if(DEFINED EXTRA_ASSEMBLY AND NOT "${EXTRA_ASSEMBLY}" STREQUAL "")
    run_checked(extra "${AS}" -target aarch64-none-elf "${EXTRA_ASSEMBLY}" -o "${OUTPUT}/extra.o")
    list(APPEND objects "${OUTPUT}/extra.o")
endif()
run_checked(link "${LD}" -T "${CMAKE_CURRENT_LIST_DIR}/bare.ld" ${objects}
            -o "${OUTPUT}/program.elf")
execute_process(COMMAND "${QEMU}" -M virt -cpu cortex-a57 -nographic -semihosting
                        -kernel "${OUTPUT}/program.elf"
                TIMEOUT 120 RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "${SOURCE} ${OPT_LEVEL} ${FLAGS} failed check ${status}\n${out}${err}")
endif()
