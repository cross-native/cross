# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
foreach(target mips-unknown-elf mipsel-unknown-elf
               mips64-unknown-elf mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -target "${target}" -O2 -c "${SOURCE}"
                -o "${OUTPUT}-${target}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} alignment expressions failed\n${stdout}\n${stderr}")
    endif()
    execute_process(
        COMMAND "${LLVM_READOBJ}" --sections "${OUTPUT}-${target}.o"
        RESULT_VARIABLE read_status OUTPUT_VARIABLE sections
        ERROR_VARIABLE read_stderr)
    if(NOT read_status EQUAL 0 OR NOT sections MATCHES
       "Name: [.]data \\([^)]*\\)[^}]*AddressAlignment: 64")
        message(FATAL_ERROR
            "${target} static alignment expression was lost\n${sections}\n${read_stderr}")
    endif()
endforeach()
