# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERRORS HARNESS HOST_CXX OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_and_check name alignment_64 alignment_128)
    execute_process(
        COMMAND "${CC}" ${ARGN} "${SOURCE}" -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} function alignment compile failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${name}.s" assembly)
    foreach(pair "aligned_64;${alignment_64}" "aligned_128;${alignment_128}")
        list(GET pair 0 symbol)
        list(GET pair 1 power)
        if(NOT assembly MATCHES "[.]p2align ${power}[^\n]*\n[.]globl ${symbol}")
            message(FATAL_ERROR "${name} ${symbol} lacks .p2align ${power}\n${assembly}")
        endif()
    endforeach()
endfunction()

compile_and_check(x86_elf 6 7 -S -O0 -target x86_64-unknown-linux-gnu)
compile_and_check(x86_coff 6 7 -S -O2 -target x86_64-w64-windows-gnu)
compile_and_check(x86_flag 8 8 -S -O0 -falign-functions=256
                  -target x86_64-unknown-linux-gnu)
compile_and_check(mips_o32 6 7 -S -O0 -target mips-unknown-elf)
compile_and_check(mips_n64_le 6 7 -S -O2 -target mips64el-unknown-elf)

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -c -O0 -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${target}.o"
        RESULT_VARIABLE object_status OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr)
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR "${target} alignment object failed\n${object_stdout}\n${object_stderr}")
    endif()
    execute_process(
        COMMAND "${LLVM_READOBJ}" --sections "${OUTPUT}-${target}.o"
        RESULT_VARIABLE read_status OUTPUT_VARIABLE sections
        ERROR_VARIABLE read_stderr)
    if(NOT read_status EQUAL 0)
        message(FATAL_ERROR "${target} section inspection failed\n${read_stderr}")
    endif()
    if(target STREQUAL "x86_64-w64-windows-gnu")
        if(NOT sections MATCHES "IMAGE_SCN_ALIGN_128BYTES")
            message(FATAL_ERROR "COFF text section lost 128-byte alignment")
        endif()
    elseif(NOT sections MATCHES
           "Name: [.]text \\([^)]*\\)[^}]*AddressAlignment: 128")
        message(FATAL_ERROR "${target} text section lost 128-byte alignment")
    endif()
endforeach()

if(WIN32)
    set(host_target x86_64-w64-windows-gnu)
else()
    set(host_target x86_64-unknown-linux-gnu)
endif()
execute_process(
    COMMAND "${HOST_CXX}" "${HARNESS}" "${OUTPUT}-${host_target}.o"
            -o "${OUTPUT}-linked.exe"
    RESULT_VARIABLE link_status OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr)
if(NOT link_status EQUAL 0)
    message(FATAL_ERROR "linked alignment harness failed\n${link_stdout}\n${link_stderr}")
endif()
execute_process(
    COMMAND "${OUTPUT}-linked.exe"
    RESULT_VARIABLE run_status OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "linked function address alignment failed: ${run_status}\n${run_stdout}\n${run_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}.ll"
    RESULT_VARIABLE llvm_status OUTPUT_VARIABLE llvm_stdout
    ERROR_VARIABLE llvm_stderr)
if(NOT llvm_status EQUAL 0)
    message(FATAL_ERROR "LLVM function alignment failed\n${llvm_stdout}\n${llvm_stderr}")
endif()
file(READ "${OUTPUT}.ll" llvm_ir)
if(NOT llvm_ir MATCHES "@\"aligned_64\"\\(\\) [^\n]*align 64" OR
   NOT llvm_ir MATCHES "@\"aligned_128\"\\(\\) [^\n]*align 128")
    message(FATAL_ERROR "LLVM function alignment metadata missing\n${llvm_ir}")
endif()
find_program(LLVM_AS NAMES llvm-as REQUIRED)
execute_process(
    COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
    RESULT_VARIABLE llvm_as_status OUTPUT_VARIABLE llvm_as_stdout
    ERROR_VARIABLE llvm_as_stderr)
if(NOT llvm_as_status EQUAL 0)
    message(FATAL_ERROR "LLVM rejected aligned definitions\n${llvm_as_stdout}\n${llvm_as_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-gimple "${SOURCE}" -o "${OUTPUT}.gimple.c"
    RESULT_VARIABLE gimple_status OUTPUT_VARIABLE gimple_stdout
    ERROR_VARIABLE gimple_stderr)
if(NOT gimple_status EQUAL 0)
    message(FATAL_ERROR "GIMPLE function alignment failed\n${gimple_stdout}\n${gimple_stderr}")
endif()
file(READ "${OUTPUT}.gimple.c" gimple_ir)
foreach(alignment 64 128)
    if(NOT gimple_ir MATCHES "aligned\\(${alignment}\\)")
        message(FATAL_ERROR "GIMPLE omitted aligned(${alignment})")
    endif()
endforeach()
find_program(HOST_GCC NAMES gcc)
if(HOST_GCC)
    execute_process(
        COMMAND "${HOST_GCC}" -c -O0 -fgimple "${OUTPUT}.gimple.c"
                -o "${OUTPUT}-gimple.o"
        RESULT_VARIABLE gcc_status OUTPUT_VARIABLE gcc_stdout
        ERROR_VARIABLE gcc_stderr)
    if(NOT gcc_status EQUAL 0)
        message(FATAL_ERROR "GCC rejected aligned GIMPLE\n${gcc_stdout}\n${gcc_stderr}")
    endif()
endif()

execute_process(
    COMMAND "${CC}" -S "${ERRORS}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr)
if(error_status EQUAL 0 OR
   NOT error_stderr MATCHES "aligned argument must be a positive power-of-two integer constant" OR
   NOT error_stderr MATCHES "aligned on function definition requires one integer argument" OR
   NOT error_stderr MATCHES "aligned requires a function definition")
    message(FATAL_ERROR "function alignment diagnostics missing\n${error_stdout}\n${error_stderr}")
endif()
