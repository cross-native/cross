# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE INDEX_ERROR MIPS_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(run_cc label)
    execute_process(
        COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

run_cc(elf-assembly -S -O2 -target x86_64-unknown-linux-gnu
       "${SOURCE}" -o "${OUTPUT}.s")
run_cc(elf-object -c -O2 -target x86_64-unknown-linux-gnu
       "${SOURCE}" -o "${OUTPUT}.elf.o")
run_cc(coff-object -c -O2 -target x86_64-w64-windows-gnu
       "${SOURCE}" -o "${OUTPUT}.coff.o")
run_cc(macho-object -c -O2 -target x86_64-apple-darwin
       "${SOURCE}" -o "${OUTPUT}.macho.o")
run_cc(llvm-debug -emit-llvm -O2 -target x86_64-unknown-linux-gnu
       "${SOURCE}" -o "${OUTPUT}.ll")

file(READ "${OUTPUT}.s" assembly)
foreach(pattern
        "patch_record:"
        "patch_records:"
        "patch_table:"
        "\\.quad \\.Lcross\\.patch\\.value\\.0\\.end-8"
        "\\.quad \\.Lcross\\.patch\\.value\\.1\\.end-8"
        "\\.quad \\.Lcross\\.patch\\.value\\.2\\.end-8"
        "\\.quad \\.Lcross\\.patch\\.0\\.end-8"
        "\\.quad patch_record"
        "11,0,0,0,0,0,0,0"
        "33,0,0,0,0,0,0,0"
        "55,0,0,0,0,0,0,0")
    if(NOT assembly MATCHES "${pattern}")
        message(FATAL_ERROR
            "structured patch assembly lacks '${pattern}'\n${assembly}")
    endif()
endforeach()

file(READ "${OUTPUT}.ll" llvm)
foreach(pattern
        "module asm"
        "patch_record:"
        "patch_records:"
        "patch_table:"
        "@\"patch_record\" = external global"
        "@\"patch_records\" = external global"
        "@\"patch_table\" = external global")
    if(NOT llvm MATCHES "${pattern}")
        message(FATAL_ERROR
            "structured patch LLVM debug output lacks '${pattern}'\n${llvm}")
    endif()
endforeach()

find_program(LLVM_AS NAMES llvm-as)
if(LLVM_AS)
    execute_process(
        COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
        RESULT_VARIABLE verify_status
        OUTPUT_VARIABLE verify_stdout
        ERROR_VARIABLE verify_stderr)
    if(NOT verify_status EQUAL 0)
        message(FATAL_ERROR
            "LLVM rejected structured patch debug IR\n"
            "${verify_stdout}\n${verify_stderr}")
    endif()
endif()

run_cc(mips-structured-big -c -target mips-unknown-elf
       "${MIPS_SOURCE}" -o "${OUTPUT}-mips.o")
run_cc(mips-structured-little -c -target mipsel-unknown-elf
       "${MIPS_SOURCE}" -o "${OUTPUT}-mipsel.o")

foreach(case RANGE 1 4)
    if(case EQUAL 1)
        set(pattern "must designate an uninitialized static-duration subobject")
    elseif(case EQUAL 4)
        set(pattern "address sink is used by more than one site")
    else()
        set(pattern "must be a static object followed only by direct member or constant array selections")
    endif()
    # A source-phase failure must not hide a separate lowering-phase case.
    execute_process(
        COMMAND "${CC}" -S "-DPATCH_ERROR_CASE=${case}" "${ERROR_SOURCE}" -o "${OUTPUT}-error-${case}.s"
        RESULT_VARIABLE error_status
        OUTPUT_VARIABLE error_stdout
        ERROR_VARIABLE error_stderr)
    string(FIND "${error_stderr}" "${pattern}" position)
    if(NOT error_status EQUAL 1 OR position EQUAL -1 OR NOT error_stderr MATCHES ":[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR
            "structured patch case ${case} lacks '${pattern}'\n${error_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${INDEX_ERROR}" -o "${OUTPUT}-index-error.s"
    RESULT_VARIABLE index_status
    OUTPUT_VARIABLE index_stdout
    ERROR_VARIABLE index_stderr)
if(NOT index_status EQUAL 1 OR NOT index_stderr MATCHES
   "runtime local or parameter is not a translation-time value" OR
   NOT index_stderr MATCHES ":[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR
        "runtime patch-sink index was not diagnosed\n"
        "${index_stdout}\n${index_stderr}")
endif()
