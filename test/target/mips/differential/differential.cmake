# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Differential execution: compile one @ABI@ kernel template twice -- once as
# the x86-64 host reference (native ABI, -O1), once for a MIPS target/CPU
# profile under test (-O2 by default) -- link each against its checked-in
# generated driver (main_<set>.cpp / start_<set>.s), run the MIPS side under
# QEMU R4000, and compare every printed kernel result line by line against
# the host reference. No Python runs at test time; generate.py only produces
# the checked-in start_*.s/main_*.cpp files, ahead of time, by hand.

foreach(required CC HOST_CXX TEMPLATE STARTUP HOST_MAIN LINKER
                 WRAPPER_TEMPLATE WRAPPER_LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(NOT DEFINED TARGET_FLAGS OR "${TARGET_FLAGS}" STREQUAL "")
    message(FATAL_ERROR "TARGET_FLAGS must name the MIPS target/CPU flags")
endif()
if(NOT DEFINED OPT_LEVEL OR "${OPT_LEVEL}" STREQUAL "")
    set(OPT_LEVEL -O2)
endif()
if(NOT DEFINED ENDIAN OR "${ENDIAN}" STREQUAL "")
    set(ENDIAN BE)
endif()

if(ENDIAN STREQUAL "BE")
    set(mips_triple mips-unknown-elf)
    set(wrapper_triple mips64-unknown-elf)
    set(elf32_format elf32btsmip)
    set(elf64_format elf64btsmip)
    set(qemu_name qemu-system-mips64)
elseif(ENDIAN STREQUAL "LE")
    set(mips_triple mipsel-unknown-elf)
    set(wrapper_triple mips64el-unknown-elf)
    set(elf32_format elf32ltsmip)
    set(elf64_format elf64ltsmip)
    set(qemu_name qemu-system-mips64el)
else()
    message(FATAL_ERROR "ENDIAN must be BE or LE, got '${ENDIAN}'")
endif()

find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLD NAMES ld.lld)
find_program(QEMU NAMES ${qemu_name})
if(NOT LLVM_MC OR NOT LLVM_OBJCOPY OR NOT LLD OR NOT QEMU)
    message(STATUS
        "skipping MIPS differential test: llvm-mc, llvm-objcopy, ld.lld, and ${qemu_name} are required")
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

function(split_flags flags out_var)
    if("${flags}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
    else()
        separate_arguments(result NATIVE_COMMAND "${flags}")
        set(${out_var} "${result}" PARENT_SCOPE)
    endif()
endfunction()

split_flags("${TARGET_FLAGS}" target_flags)
split_flags("${EXTRA_FLAGS}" extra_flags)

# The harness sides are C++ (host) and freestanding assembly (MIPS), so both
# intentionally opt out of the default Cross ABI -- same reasoning as
# test/support/run_native.cmake.
if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()
set(MIPS_ABI o32)

file(READ "${TEMPLATE}" template_source)
string(REPLACE "@ABI@" "${HOST_ABI}" host_source "${template_source}")
string(REPLACE "@ABI@" "${MIPS_ABI}" mips_source "${template_source}")
set(host_x "${OUTPUT}.host.x")
set(mips_x "${OUTPUT}.mips.x")
file(WRITE "${host_x}" "${host_source}")
file(WRITE "${mips_x}" "${mips_source}")

# --- host x86-64 reference ---
set(host_object "${OUTPUT}.host.o")
set(host_exe "${OUTPUT}.host.exe")
run_checked(host-compile "${CC}" "-mabi=${HOST_ABI}" -O1 -c
            "${host_x}" -o "${host_object}")
run_checked(host-link "${HOST_CXX}" "${HOST_MAIN}" "${host_object}"
            -o "${host_exe}")
execute_process(
    COMMAND "${host_exe}"
    RESULT_VARIABLE host_run_status
    OUTPUT_VARIABLE expected_output
    ERROR_VARIABLE host_run_stderr)
if(NOT host_run_status EQUAL 0)
    message(FATAL_ERROR
        "host reference run failed (${host_run_status})\n${expected_output}\n${host_run_stderr}")
endif()

# --- MIPS target under QEMU ---
set(mips_object "${OUTPUT}.mips.o")
set(start_object "${OUTPUT}.start.o")
set(elf32 "${OUTPUT}.elf32")
set(image "${OUTPUT}.bin")
set(wrapper_source "${OUTPUT}.wrapper.s")
set(wrapper_object "${OUTPUT}.wrapper.o")
set(elf64 "${OUTPUT}.elf64")

run_checked(mips-compile "${CC}" -c "${OPT_LEVEL}" ${target_flags} ${extra_flags}
            "${mips_x}" -o "${mips_object}")
# The startup is MIPS III code for the QEMU host but must carry a 32-bit FPU
# tag: MIPS I objects are FP32 and VR4300 objects are FPXX.
run_checked(startup "${LLVM_MC}" --filetype=obj
            --triple=${mips_triple} --mcpu=mips3
            --mattr=+noabicalls,-fp64 "${STARTUP}" -o "${start_object}")
run_checked(link-elf32 "${LLD}" -m ${elf32_format} -T "${LINKER}"
            "${start_object}" "${mips_object}" -o "${elf32}")
run_checked(flatten-image "${LLVM_OBJCOPY}" -O binary "${elf32}" "${image}")

# See test/target/mips/runtime.cmake for why the wrapper exists: Malta's
# 64-bit firmware wants a sign-extended 64-bit kernel entry, so the already
# relocated o32 image is transported into KSEG0 unmodified -- every
# instruction under test remains the exact llvm-mc o32 output.
file(TO_CMAKE_PATH "${image}" IMAGE)
configure_file("${WRAPPER_TEMPLATE}" "${wrapper_source}" @ONLY)
run_checked(wrapper "${LLVM_MC}" --filetype=obj
            --triple=${wrapper_triple} --mcpu=mips3
            "${wrapper_source}" -o "${wrapper_object}")
run_checked(link-wrapper "${LLD}" -m ${elf64_format} -T "${WRAPPER_LINKER}"
            "${wrapper_object}" -o "${elf64}")

execute_process(
    COMMAND "${QEMU}" -M malta -cpu R4000 -m 64M -bios none
            -kernel "${elf64}" -display none
            -serial none -serial none -serial stdio -monitor none
            -no-reboot -semihosting
    RESULT_VARIABLE qemu_status
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE qemu_stderr
    TIMEOUT 60)
if(NOT qemu_status EQUAL 0)
    message(FATAL_ERROR
        "MIPS execution did not complete (status ${qemu_status})\n${actual_output}\n${qemu_stderr}")
endif()

# --- compare line by line ---
string(REPLACE "\r\n" "\n" expected_clean "${expected_output}")
string(REPLACE "\r\n" "\n" actual_clean "${actual_output}")
string(STRIP "${expected_clean}" expected_clean)
string(STRIP "${actual_clean}" actual_clean)
if("${expected_clean}" STREQUAL "")
    message(FATAL_ERROR "host reference produced no output")
endif()
string(REPLACE "\n" ";" expected_lines "${expected_clean}")
if("${actual_clean}" STREQUAL "")
    set(actual_lines "")
else()
    string(REPLACE "\n" ";" actual_lines "${actual_clean}")
endif()
list(LENGTH expected_lines expected_count)
list(LENGTH actual_lines actual_count)

file(STRINGS "${TEMPLATE}" kernel_lines REGEX "global u32 (k_[A-Za-z0-9_]+)\\(")
set(kernel_names "")
foreach(kernel_line IN LISTS kernel_lines)
    string(REGEX REPLACE ".*global u32 (k_[A-Za-z0-9_]+)\\(.*" "\\1"
           kernel_name "${kernel_line}")
    list(APPEND kernel_names "${kernel_name}")
endforeach()

set(mismatches "")
math(EXPR last_index "${expected_count} - 1")
foreach(i RANGE 0 ${last_index})
    list(GET expected_lines ${i} expected_value)
    if(i LESS actual_count)
        list(GET actual_lines ${i} actual_value)
    else()
        set(actual_value "<missing>")
    endif()
    if(NOT "${actual_value}" STREQUAL "${expected_value}")
        math(EXPR kernel_index "${i} / 6")
        math(EXPR arg_index "${i} % 6")
        list(GET kernel_names ${kernel_index} kernel_name)
        list(APPEND mismatches
             "MISMATCH ${kernel_name} args#${arg_index}: expected ${expected_value} got ${actual_value}")
    endif()
endforeach()

list(LENGTH mismatches bad_count)
if(bad_count GREATER 0 OR NOT expected_count EQUAL actual_count)
    list(JOIN mismatches "\n" mismatch_text)
    message(FATAL_ERROR
        "differential mismatch: expected ${expected_count} values, got ${actual_count}\n${mismatch_text}")
endif()

message(STATUS "differential PASS: ${expected_count} values matched (${OUTPUT})")
