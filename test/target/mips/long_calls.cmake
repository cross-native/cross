# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -mlong-calls reaches every callee through a register; without it a call is
# a jal and a tail call a j, which name only the caller's 256 MB region.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

function(compile name)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: compilation failed\n${errors}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(expect name pattern)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${name} lacks '${pattern}'\n${text}")
    endif()
endfunction()

set(o32 -target mips-unknown-elf -march=vr4300 -mabi=o32)
set(far "\tlui\t[$]at,%hi[(]far_callee[)]\n\taddiu\t[$]at,[$]at,%lo[(]far_callee[)]\n")

compile(long ${o32} -mlong-calls)
string(REGEX MATCHALL "${far}\tjalr\t[$]at\n" calls "${text}")
list(LENGTH calls call_count)
if(NOT call_count EQUAL 2 OR text MATCHES "\tjal\t")
    message(FATAL_ERROR "long calls kept a jal or lost a jalr\n${text}")
endif()
expect(long "\tlui\t[$]at,%hi[(]__cross_static_[0-9]+_helper[)]\n[^\n]*\n\tjalr\t[$]at\n")
expect(long "${far}\tjr\t[$]at\n")

compile(short ${o32})
expect(short "\tjal\tfar_callee\n")
expect(short "\tj\tfar_callee\n")
if(text MATCHES "jalr")
    message(FATAL_ERROR "short calls use jalr\n${text}")
endif()

compile(last ${o32} -mlong-calls -mno-long-calls)
expect(last "\tjal\tfar_callee\n")

compile(n64 -target mips64-unknown-elf -mlong-calls)
expect(n64 "\tlui\t[$]1,%hi[(]far_callee[)]\n\tdaddiu\t[$]1,[$]1,%lo[(]far_callee[)]\n\tjalr\t[$]1\n")

# The object writer accepts the sequence.
find_program(LLVM_MC NAMES llvm-mc)
if(LLVM_MC)
    execute_process(COMMAND "${CC}" -c -O2 ${o32} -mlong-calls "${SOURCE}"
                            -o "${OUTPUT}-long.o"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "long calls do not assemble\n${errors}")
    endif()
endif()
