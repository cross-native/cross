# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(extract_function assembly symbol variable)
    string(FIND "${assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "could not find ${symbol}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    if(length EQUAL -1)
        string(FIND "${tail}" ".seh_endproc" length)
    endif()
    if(length EQUAL -1)
        message(FATAL_ERROR "could not find the end of ${symbol}")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu)
    string(REPLACE "-" "_" suffix "${target}")
    execute_process(
        COMMAND "${CC}" -S -O0 -funwind-tables -fno-eval-calls -target "${target}"
                "${SOURCE}" -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "default Cross ABI failed for ${target}\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" assembly)
    extract_function("${assembly}" cross_default_seven cross_body)
    foreach(reg r10 r9 r8 rcx rdx r11 rax)
        if(NOT cross_body MATCHES "%${reg}")
            message(FATAL_ERROR
                "default Cross ABI did not use ${reg} for ${target}\n${cross_body}")
        endif()
    endforeach()
    extract_function("${assembly}" explicit_sysv sysv_body)
    if(NOT sysv_body MATCHES "%rdi")
        message(FATAL_ERROR "explicit SysV ABI was not honored\n${sysv_body}")
    endif()
    extract_function("${assembly}" explicit_ms ms_body)
    if(NOT ms_body MATCHES "%rcx")
        message(FATAL_ERROR "explicit MS ABI was not honored\n${ms_body}")
    endif()

    execute_process(
        COMMAND "${CC}" -target "${target}" --print-abis
        RESULT_VARIABLE registry_status
        OUTPUT_VARIABLE registry
        ERROR_VARIABLE registry_stderr
    )
    if(NOT registry_status EQUAL 0 OR NOT registry MATCHES "default: cross")
        message(FATAL_ERROR
            "${target} does not select the Cross ABI by default\n"
            "${registry}\n${registry_stderr}")
    endif()
endforeach()

foreach(pair "sysv_abi;rdi" "ms_abi;rcx")
    list(GET pair 0 abi)
    list(GET pair 1 register)
    execute_process(
        COMMAND "${CC}" -S -O0 -funwind-tables -fno-eval-calls "-mabi=${abi}"
                "${SOURCE}" -o "${OUTPUT}-${abi}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "whole-compilation ${abi} selection failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}-${abi}.s" assembly)
    extract_function("${assembly}" cross_default_seven body)
    if(NOT body MATCHES "%${register}")
        message(FATAL_ERROR "-mabi=${abi} was not applied\n${body}")
    endif()
endforeach()

# Redeclarations must select the same ABI entry; an alias of it agrees.
file(WRITE "${OUTPUT}-conflict.x" [=[
[[abi("sysv_abi")]] global u64 conflict(in u64 value);
[[abi("ms")]] global u64 conflict(in u64 value) { return value; }
]=])
execute_process(
    COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${OUTPUT}-conflict.x"
            -o "${OUTPUT}-conflict.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(status EQUAL 0 OR NOT compile_stderr MATCHES
   "conflict[.]x:2:[0-9]+: error: incompatible redeclaration of function 'conflict'")
    message(FATAL_ERROR "conflicting ABI redeclaration was not diagnosed\n"
        "${compile_stdout}\n${compile_stderr}")
endif()
