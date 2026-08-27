# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE PAIR_SOURCE RUNTIME MODULE_INFO LINKER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLVM_READOBJ NAMES llvm-readobj)
find_program(LLD NAMES ld.lld)
if(NOT DEFINED PPSSPP OR "${PPSSPP}" STREQUAL "" OR
   NOT EXISTS "${PPSSPP}")
    find_program(PPSSPP_DISCOVERED NAMES PPSSPPHeadless PPSSPPHeadless.exe)
    if(PPSSPP_DISCOVERED)
        set(PPSSPP "${PPSSPP_DISCOVERED}")
    endif()
endif()
if(NOT LLVM_MC OR NOT LLVM_OBJCOPY OR NOT LLVM_READOBJ OR NOT LLD OR
   NOT PPSSPP OR NOT EXISTS "${PPSSPP}")
    message(STATUS
        "skipping PSP runtime test: LLVM MC tools, ld.lld, and PPSSPPHeadless are required")
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

function(fatal_with_log headline variable)
    set(log "${${variable}}")
    string(LENGTH "${log}" log_length)
    if(log_length GREATER 12000)
        string(SUBSTRING "${log}" 0 6000 log_head)
        math(EXPR log_tail_offset "${log_length} - 6000")
        string(SUBSTRING "${log}" ${log_tail_offset} 6000 log_tail)
        set(log "${log_head}\n... PPSSPP log truncated ...\n${log_tail}")
    endif()
    message(FATAL_ERROR "${headline}\n${log}")
endfunction()

set(functions "${OUTPUT}.functions.o")
set(pairs "${OUTPUT}.pairs.o")
set(runtime "${OUTPUT}.runtime.o")
set(metadata_object "${OUTPUT}.metadata.o")
set(metadata_binary "${OUTPUT}.metadata.bin")
set(executable "${OUTPUT}.elf")

run_checked(cross-functions "${CC}" -c -O2 -mprofile=psp-allegrex
            "${SOURCE}" -o "${functions}")
run_checked(cross-pairs "${CC}" -c -O2 -mprofile=psp-allegrex
            "${PAIR_SOURCE}" -o "${pairs}")
run_checked(cross-runtime "${CC}" -c -O2 -mprofile=psp-allegrex
            "${RUNTIME}" -o "${runtime}")
run_checked(module-metadata "${LLVM_MC}" --filetype=obj
            --triple=mipsel-unknown-elf --mcpu=mips2
            "${MODULE_INFO}" -o "${metadata_object}")
run_checked(flatten-module-metadata "${LLVM_OBJCOPY}"
            "--dump-section=.rodata.sceModuleInfo=${metadata_binary}"
            "${metadata_object}")
run_checked(link "${LLD}" -m elf32ltsmip -T "${LINKER}"
            "${runtime}" "${functions}" "${pairs}"
            --format=binary "${metadata_binary}"
            -o "${executable}")

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers --program-headers "${executable}"
    RESULT_VARIABLE header_status
    OUTPUT_VARIABLE header
    ERROR_VARIABLE header_stderr)
if(NOT header_status EQUAL 0)
    message(FATAL_ERROR "reading PSP ELF header failed\n${header_stderr}")
endif()
foreach(expect "Format: elf32-mips" "DataEncoding: LittleEndian"
               "EF_MIPS_ABI_EABI32" "EF_MIPS_ARCH_2"
               "VirtualAddress: 0x8804000")
    if(NOT header MATCHES "${expect}")
        message(FATAL_ERROR
            "PSP executable is missing '${expect}'\n${header}")
    endif()
endforeach()

get_filename_component(test_root "${OUTPUT}" DIRECTORY)
execute_process(
    COMMAND "${PPSSPP}" --graphics=software --timeout=5 -i -l
            --root "${test_root}" "${executable}"
    RESULT_VARIABLE ppsspp_status
    OUTPUT_VARIABLE ppsspp_stdout
    ERROR_VARIABLE ppsspp_stderr
    TIMEOUT 15)
set(ppsspp_log "${ppsspp_stdout}\n${ppsspp_stderr}")
if(NOT ppsspp_status EQUAL 0)
    fatal_with_log(
        "Allegrex execution failed (status ${ppsspp_status})" ppsspp_log)
endif()
foreach(expect "Loadable Segment Copied to 08804000"
               "Importing Module LoadExecForUser"
               "sceKernel: sceKernelExitGame")
    if(NOT ppsspp_log MATCHES "${expect}")
        fatal_with_log("PPSSPP did not report '${expect}'" ppsspp_log)
    endif()
endforeach()
