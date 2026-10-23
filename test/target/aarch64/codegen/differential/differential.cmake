# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Differential execution: compiles one @ABI@ kernel template twice -- as the
# x86-64 host reference (its C ABI, -O1) and for AArch64 under ABI at
# OPT_LEVEL -- links the AArch64 objects with the checked-in startup through
# the Cross as and ld, runs the image on QEMU's virt machine, and compares
# every printed kernel result with the host's, line by line.

foreach(required CC AS LD HOST_CXX TEMPLATE STARTUP HOST_MAIN SCRIPT ABI OUTPUT)
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

separate_arguments(extra_flags NATIVE_COMMAND "${EXTRA_FLAGS}")
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")

# The host side is C++, so its kernels take the host's C ABI.
if(WIN32)
    set(host_abi ms_abi)
else()
    set(host_abi sysv_abi)
endif()
file(READ "${TEMPLATE}" template_source)
string(REPLACE "@ABI@" "${host_abi}" host_source "${template_source}")
string(REPLACE "@ABI@" "${ABI}" target_source "${template_source}")
file(WRITE "${OUTPUT}.host.x" "${host_source}")
file(WRITE "${OUTPUT}.aarch64.x" "${target_source}")

# The host reference.
run_checked(host-compile "${CC}" -mabi=${host_abi} -O1 -c
            "${OUTPUT}.host.x" -o "${OUTPUT}.host.o")
run_checked(host-link "${HOST_CXX}" "${HOST_MAIN}" "${OUTPUT}.host.o" -o "${OUTPUT}.host.exe")
execute_process(COMMAND "${OUTPUT}.host.exe" RESULT_VARIABLE status
                OUTPUT_VARIABLE expected ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "host reference run failed (${status})\n${expected}\n${err}")
endif()

# The AArch64 image.
run_checked(compile "${CC}" -target ${TARGET} ${OPT_LEVEL} ${extra_flags} -c
            "${OUTPUT}.aarch64.x" -o "${OUTPUT}.aarch64.o")
run_checked(startup "${AS}" -target aarch64-none-elf "${STARTUP}" -o "${OUTPUT}.start.o")
run_checked(link "${LD}" -T "${SCRIPT}" "${OUTPUT}.start.o" "${OUTPUT}.aarch64.o"
            -o "${OUTPUT}.elf")
execute_process(COMMAND "${QEMU}" -M virt -cpu cortex-a57 -nographic -semihosting
                        -kernel "${OUTPUT}.elf"
                TIMEOUT 120 RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE err)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "AArch64 execution did not complete (${status})\n${actual}\n${err}")
endif()

string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual "${actual}")
string(STRIP "${expected}" expected)
string(STRIP "${actual}" actual)
string(REPLACE "\n" ";" expected_lines "${expected}")
string(REPLACE "\n" ";" actual_lines "${actual}")
list(LENGTH expected_lines expected_count)
list(LENGTH actual_lines actual_count)
file(STRINGS "${TEMPLATE}" kernel_lines REGEX "global u32 (k_[A-Za-z0-9_]+)\\(")
set(kernels "")
foreach(kernel_line IN LISTS kernel_lines)
    string(REGEX REPLACE ".*global u32 (k_[A-Za-z0-9_]+)\\(.*" "\\1" kernel "${kernel_line}")
    list(APPEND kernels "${kernel}")
endforeach()
set(mismatches "")
math(EXPR last "${expected_count} - 1")
foreach(index RANGE 0 ${last})
    list(GET expected_lines ${index} want)
    set(got "<missing>")
    if(index LESS actual_count)
        list(GET actual_lines ${index} got)
    endif()
    if(NOT "${got}" STREQUAL "${want}")
        math(EXPR kernel_index "${index} / 6")
        math(EXPR argument_index "${index} % 6")
        list(GET kernels ${kernel_index} kernel)
        list(APPEND mismatches "${kernel} arguments #${argument_index}: expected ${want}, got ${got}")
    endif()
endforeach()
if(mismatches OR NOT expected_count EQUAL actual_count)
    list(JOIN mismatches "\n" text)
    message(FATAL_ERROR
        "${expected_count} values expected, ${actual_count} printed\n${text}")
endif()
message(STATUS "${expected_count} values matched")
