# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(reject_disabled name)
    execute_process(
        COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.disabled.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0 OR
       NOT stderr MATCHES
           "target instruction '[$]::_vzeroupper' requires feature 'avx'")
        message(FATAL_ERROR
            "disabled AVX raw instruction was not rejected\n${stdout}\n${stderr}")
    endif()
endfunction()

reject_disabled(base)
reject_disabled(cpu -march=haswell -mno-avx)

function(compile_enabled name flag)
    execute_process(
        COMMAND "${CC}" -c "${flag}" "${SOURCE}"
                -o "${OUTPUT}.${name}.o"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "enabled AVX raw instruction failed (${name})\n${stdout}\n${stderr}")
    endif()
    execute_process(
        COMMAND "${CC}" -S "${flag}" "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    if(NOT assembly_status EQUAL 0)
        message(FATAL_ERROR
            "enabled AVX assembly failed (${name})\n"
            "${assembly_stdout}\n${assembly_stderr}")
    endif()
    file(READ "${OUTPUT}.${name}.s" assembly)
    if(NOT assembly MATCHES "vzeroupper")
        message(FATAL_ERROR "AVX instruction selection was lost\n${assembly}")
    endif()
endfunction()

compile_enabled(avx -mavx)
compile_enabled(cpu -march=haswell)

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_error
)
string(FIND "${registry}" "$::_vzeroupper instruction [avx]" registry_position)
if(NOT registry_status EQUAL 0 OR registry_position EQUAL -1)
    message(FATAL_ERROR
        "instruction registry lost the AVX requirement\n${registry}\n${registry_error}")
endif()
