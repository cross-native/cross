# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE BAD_SOURCE BAD_CAST_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(target IN ITEMS x86-linux x86-windows x86-macos
                        mips mipsel mips64 mips64el)
    if(target STREQUAL x86-linux)
        set(flags -target x86_64-unknown-linux-gnu)
    elseif(target STREQUAL x86-windows)
        set(flags -target x86_64-w64-windows-gnu)
    elseif(target STREQUAL x86-macos)
        set(flags -target x86_64-apple-darwin)
    elseif(target STREQUAL mips)
        set(flags -target mips-unknown-elf -march=r3000 -mabi=o32)
    elseif(target STREQUAL mipsel)
        set(flags -target mipsel-unknown-elf -march=r3000 -mabi=o32)
    elseif(target STREQUAL mips64)
        set(flags -target mips64-unknown-elf -march=mips64 -mabi=n64)
    else()
        set(flags -target mips64el-unknown-elf -march=mips64 -mabi=n64)
    endif()
    execute_process(
        COMMAND "${CC}" ${flags} -S "${SOURCE}" -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "relocatable static source failed (${target})\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${target}.s" assembly)
    foreach(pattern "target\\+16" "target\\+24" "target-8"
                    "object\\+4" "nested\\+12"
                    "nested\\+8")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR "${target} output lacks relocation ${pattern}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${BAD_SOURCE}" -o "${OUTPUT}-bad.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR NOT err MATCHES "runtime/static storage cannot be read during translation-time evaluation" OR
   NOT err MATCHES "relocatable_addresses_bad\\.x:[0-9]+:[0-9]+")
    message(FATAL_ERROR "missing runtime-scalar address-initializer rejection (${status})\n${out}\n${err}")
endif()

execute_process(
    COMMAND "${CC}" -S "${BAD_CAST_SOURCE}" -o "${OUTPUT}-bad-cast.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "not an address constant" OR
   NOT err MATCHES "relocatable_addresses_bad_cast\\.x:[0-9]+:[0-9]+")
    message(FATAL_ERROR "incompatible static pointer cast was accepted\n${out}\n${err}")
endif()

if(LLVM_TEXT)
    find_program(LLVM_AS NAMES llvm-as)
    if(LLVM_AS)
        execute_process(
            COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}.ll"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "relocatable LLVM serialization failed\n${out}\n${err}")
        endif()
        execute_process(
            COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "relocatable LLVM verification failed\n${out}\n${err}")
        endif()
    else()
        message(STATUS "skipping optional relocatable LLVM verification: llvm-as unavailable")
    endif()
endif()
