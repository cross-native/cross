# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE HARNESS OUTPUT)
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
    COMMAND "${CC}" "-mabi=${HOST_ABI}" -S -O2 -v
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
if(NOT assembly_status EQUAL 0)
    message(FATAL_ERROR
        "native assembly emission failed (${assembly_status})\n"
        "${assembly_stdout}\n${assembly_stderr}")
endif()
if(NOT assembly_stderr MATCHES "selecting native x86-64 Machine IR" OR
   assembly_stderr MATCHES "LLVM fallback")
    message(FATAL_ERROR
        "managed-only source did not select the in-tree native backend\n"
        "${assembly_stderr}")
endif()

file(READ "${OUTPUT}.s" assembly)
foreach(pattern
        "native_entry:"
        "native_loop:"
        "native_divrem:"
        "native_rotate:"
        "idivq"
        "divq"
        "rolq"
        ".text.cross.patch."
        ".Lcross.patch.value.")
    if(NOT assembly MATCHES "${pattern}")
        message(FATAL_ERROR "native assembly is missing '${pattern}'")
    endif()
endforeach()
string(REGEX MATCHALL "[\t ]divq[\t ]" unsigned_divisions "${assembly}")
list(LENGTH unsigned_divisions unsigned_division_count)
if(NOT unsigned_division_count EQUAL 1)
    message(FATAL_ERROR
        "native_divrem should share one division between / and %, found "
        "${unsigned_division_count}\n${assembly}")
endif()
if(assembly MATCHES "(__divti3|__udivti3|memcpy|memset)")
    message(FATAL_ERROR "native assembly introduced a hidden runtime symbol")
endif()

execute_process(
    COMMAND "${CC}" "-mabi=${HOST_ABI}" -S -O2 -fno-peephole2
            "${SOURCE}" -o "${OUTPUT}.no-peephole.s"
    RESULT_VARIABLE no_peephole_status
    OUTPUT_VARIABLE no_peephole_stdout
    ERROR_VARIABLE no_peephole_stderr
)
if(NOT no_peephole_status EQUAL 0)
    message(FATAL_ERROR
        "native no-peephole assembly emission failed\n"
        "${no_peephole_stdout}\n${no_peephole_stderr}")
endif()
file(READ "${OUTPUT}.no-peephole.s" no_peephole_assembly)
if(no_peephole_assembly MATCHES "[\t ]rolq[\t ]")
    message(FATAL_ERROR
        "-fno-peephole2 did not disable MIR rotate recognition")
endif()

execute_process(
    COMMAND "${CC}" "-mabi=${HOST_ABI}" -S -O2 -fno-tree-ccp
            "${SOURCE}" -o "${OUTPUT}.no-tree-ccp.s"
    RESULT_VARIABLE no_tree_ccp_status
    OUTPUT_VARIABLE no_tree_ccp_stdout
    ERROR_VARIABLE no_tree_ccp_stderr
)
if(NOT no_tree_ccp_status EQUAL 0)
    message(FATAL_ERROR
        "native no-tree-ccp assembly emission failed\n"
        "${no_tree_ccp_stdout}\n${no_tree_ccp_stderr}")
endif()
file(READ "${OUTPUT}.no-tree-ccp.s" no_tree_ccp_assembly)
if(NOT no_tree_ccp_assembly MATCHES "[\t ]rolq[\t ]")
    message(FATAL_ERROR
        "-fpeephole2 was incorrectly coupled to -ftree-ccp")
endif()

execute_process(
    COMMAND "${CC}" "-mabi=${HOST_ABI}" -c -O2
            "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "native COFF object emission failed (${object_status})\n"
        "${object_stdout}\n${object_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -c -O2 -target x86_64-unknown-linux-gnu
            -mabi=sysv_abi
            "${SOURCE}" -o "${OUTPUT}.elf.o"
    RESULT_VARIABLE elf_status
    OUTPUT_VARIABLE elf_stdout
    ERROR_VARIABLE elf_stderr
)
if(NOT elf_status EQUAL 0)
    message(FATAL_ERROR
        "native ELF object emission failed (${elf_status})\n"
        "${elf_stdout}\n${elf_stderr}")
endif()

execute_process(
    COMMAND clang "${HARNESS}" "${OUTPUT}.o" -o "${OUTPUT}.exe"
    RESULT_VARIABLE link_status
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)
if(NOT link_status EQUAL 0)
    message(FATAL_ERROR
        "native runtime harness link failed (${link_status})\n"
        "${link_stdout}\n${link_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT}.exe"
    RESULT_VARIABLE runtime_status
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_stderr
)
if(NOT runtime_status EQUAL 0)
    message(FATAL_ERROR
        "native runtime result failed (${runtime_status})\n"
        "${runtime_stdout}\n${runtime_stderr}")
endif()
