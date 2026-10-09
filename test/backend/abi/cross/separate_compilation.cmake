# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX CALLEE_SOURCE CALLER_SOURCE RUNNER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()

execute_process(
    COMMAND "${CC}" -c -O2 -funwind-tables -fno-eval-calls "${CALLEE_SOURCE}"
            -o "${OUTPUT}.callee.o"
    RESULT_VARIABLE callee_status
    OUTPUT_VARIABLE callee_stdout
    ERROR_VARIABLE callee_stderr
)
execute_process(
    COMMAND "${CC}" -c -O2 -funwind-tables -fno-eval-calls "-mabi=${HOST_ABI}"
            "${CALLER_SOURCE}" -o "${OUTPUT}.caller.o"
    RESULT_VARIABLE caller_status
    OUTPUT_VARIABLE caller_stdout
    ERROR_VARIABLE caller_stderr
)
# Win64 also preserves RSI, RDI, and XMM6-XMM15, which the Cross ABI lets a
# callee change. SysV preserves the same GPRs as Cross and no SIMD storage,
# so a SysV caller needs no bridge.
execute_process(
    COMMAND "${CC}" -S -O2 -funwind-tables -fno-eval-calls
            -target x86_64-w64-windows-gnu -mabi=ms_abi
            "${CALLER_SOURCE}" -o "${OUTPUT}.caller.s"
    RESULT_VARIABLE caller_assembly_status
    OUTPUT_VARIABLE caller_assembly_stdout
    ERROR_VARIABLE caller_assembly_stderr
)
if(NOT callee_status EQUAL 0 OR NOT caller_status EQUAL 0)
    message(FATAL_ERROR
        "separate Cross ABI compilation failed\n"
        "${callee_stdout}\n${callee_stderr}\n"
        "${caller_stdout}\n${caller_stderr}")
endif()
if(NOT caller_assembly_status EQUAL 0)
    message(FATAL_ERROR
        "Cross ABI caller bridge assembly failed\n"
        "${caller_assembly_stdout}\n${caller_assembly_stderr}")
endif()
file(READ "${OUTPUT}.caller.s" caller_assembly)
foreach(pattern "[.]seh_(savereg|pushreg) %rsi" "[.]seh_(savereg|pushreg) %rdi"
                "[.]seh_savexmm %xmm6," "[.]seh_savexmm %xmm15,")
    if(NOT caller_assembly MATCHES "${pattern}")
        message(FATAL_ERROR
            "MS-to-Cross bridge did not preserve the stronger caller "
            "contract ('${pattern}')\n${caller_assembly}")
    endif()
endforeach()

execute_process(
    COMMAND "${HOST_CXX}" -DCROSS_ENTRY=cross_link_entry "${RUNNER}"
            "${OUTPUT}.caller.o" "${OUTPUT}.callee.o"
            -o "${OUTPUT}.exe"
    RESULT_VARIABLE link_status
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)
if(NOT link_status EQUAL 0)
    message(FATAL_ERROR
        "separate Cross ABI link failed\n${link_stdout}\n${link_stderr}")
endif()

execute_process(COMMAND "${OUTPUT}.exe" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 1)
    message(FATAL_ERROR
        "separate Cross ABI call returned ${run_status}, expected 1")
endif()
