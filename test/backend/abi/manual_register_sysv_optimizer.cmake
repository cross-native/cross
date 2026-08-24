# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

if(NOT DEFINED TARGET OR "${TARGET}" STREQUAL "")
    set(TARGET "x86_64-unknown-linux-gnu")
endif()
if(NOT DEFINED O0_OUTPUT OR "${O0_OUTPUT}" STREQUAL "")
    set(O0_OUTPUT "${OUTPUT}.O0.s")
endif()
if(NOT DEFINED O2_OUTPUT OR "${O2_OUTPUT}" STREQUAL "")
    set(O2_OUTPUT "${OUTPUT}.O2.s")
endif()

function(compile_level level output)
    execute_process(
        COMMAND "${CC}" -S "-${level}" -target "${TARGET}" "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "cc -${level} failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

compile_level(O0 "${O0_OUTPUT}")
compile_level(O2 "${O2_OUTPUT}")
file(READ "${O0_OUTPUT}" assembly_o0)
file(READ "${O2_OUTPUT}" assembly_o2)

set(entry "sysv_register_optimizer_entry")
set(local_function "__cross_group_sysv_private_step")
foreach(level o0 o2)
    set(assembly "${assembly_${level}}")
    if(NOT assembly MATCHES "${entry}:")
        message(FATAL_ERROR "-${level} assembly lost the global entry definition")
    endif()
    if(NOT assembly MATCHES "${local_function}:")
        message(FATAL_ERROR
            "-${level} assembly lost the compilation-group-local manual function")
    endif()
    if(NOT assembly MATCHES "callq?[	 ]+${local_function}")
        message(FATAL_ERROR
            "-${level} assembly lost the manual-register call boundary")
    endif()
    if(assembly MATCHES "[.]globl[	 ]+${local_function}")
        message(FATAL_ERROR
            "-${level} assembly exported a compilation-group-local function")
    endif()

    string(REGEX MATCHALL "callq?[	 ]+[^ 	\r\n#]+" calls "${assembly}")
    foreach(call IN LISTS calls)
        if(NOT call MATCHES "^callq?[	 ]+${local_function}$")
            message(FATAL_ERROR
                "-${level} assembly contains a hidden external call: ${call}")
        endif()
    endforeach()

    if(assembly MATCHES
       "(memcpy|memmove|memset|memcmp|alloca|__chkstk|__main|___main|__stack_chk|__[a-z]*div|__[a-z]*mod|@PLT)")
        message(FATAL_ERROR "-${level} assembly contains a hidden runtime reference")
    endif()
endforeach()

# The native bootstrap allocator is spill-home based at both levels. This test
# protects the manual endpoint contract and absence of hidden runtime calls;
# register promotion has its own future allocator tests.
if(NOT assembly_o0 MATCHES "[-]?[0-9]+[(]%rsp[)]")
    message(FATAL_ERROR "-O0 assembly did not exercise stack-based lowering")
endif()

# The caller and callee expose the expected x86-64 SysV endpoints: rdi/rsi
# inputs, rsi copy-out, and rax ordinary result.
foreach(pattern
        "%rdi"
        "%rsi"
        "%rax"
        "callq?[	 ]+${local_function}")
    if(NOT assembly_o2 MATCHES "${pattern}")
        message(FATAL_ERROR
            "native SysV assembly is missing pattern: ${pattern}")
    endif()
endforeach()
