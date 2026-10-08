# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE BAD_SOURCE TLS_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj)
if(LLVM_READOBJ)
    execute_process(
        COMMAND "${CC}" -target x86_64-unknown-linux-gnu -c "${SOURCE}"
                -o "${OUTPUT}-linux.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "relocatable patch ELF object failed\n${out}\n${err}")
    endif()
    execute_process(
        COMMAND "${LLVM_READOBJ}" --relocations "${OUTPUT}-linux.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR
       NOT out MATCHES "R_X86_64_64 patch_target 0x10" OR
       NOT out MATCHES "R_X86_64_64 patch_helper 0x0")
        message(FATAL_ERROR "ELF patch immediate relocations missing\n${out}\n${err}")
    endif()
    string(REGEX MATCHALL "R_X86_64_64 \\.text\\.cross\\.patch"
           sink_relocations "${out}")
    list(LENGTH sink_relocations sink_count)
    if(sink_count LESS 2)
        message(FATAL_ERROR "ELF patch sink relocations missing\n${out}")
    endif()
else()
    message(STATUS "skipping optional patch ELF relocation inspection: llvm-readobj unavailable")
endif()

execute_process(
    COMMAND "${CC}" -S "${BAD_SOURCE}" -o "${OUTPUT}-shadow.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES
   "cannot depend on an automatic local or parameter")
    message(FATAL_ERROR "shadowed runtime cell became a patch relocation\n${out}\n${err}")
endif()

execute_process(
    COMMAND "${CC}" -S "${TLS_SOURCE}" -o "${OUTPUT}-tls.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR NOT err MATCHES
   "a thread-local address is not an ordinary static relocation" OR
   NOT err MATCHES "patch_relocatable_tls.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "TLS address became an ordinary patch relocation\n${out}\n${err}")
endif()

foreach(target IN ITEMS linux windows macos)
    if(target STREQUAL linux)
        set(flags -target x86_64-unknown-linux-gnu)
    elseif(target STREQUAL windows)
        set(flags -target x86_64-w64-windows-gnu)
    else()
        set(flags -target x86_64-apple-darwin)
    endif()
    execute_process(
        COMMAND "${CC}" ${flags} -S "${SOURCE}" -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "relocatable patch source failed (${target})\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${target}.s" assembly)
    if(NOT assembly MATCHES "movabsq[ \t]+\\$[_]?patch_target\\+16" OR
       NOT assembly MATCHES "movabsq[ \t]+\\$[_]?patch_helper")
        message(FATAL_ERROR "${target} output lacks relocatable patch immediates")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -target mips-unknown-elf -march=r3000 -mabi=o32
            -S "${SOURCE}" -o "${OUTPUT}-mips.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "cannot encode a symbol relocation")
    message(FATAL_ERROR "MIPS accepted unsupported patch relocation\n${out}\n${err}")
endif()

if(LLVM_TEXT)
    execute_process(
        COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}.ll"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES
       "cannot preserve a relocatable \\$::patch immediate")
        message(FATAL_ERROR "LLVM serializer accepted unfaithful patch relocation\n${out}\n${err}")
    endif()
endif()
