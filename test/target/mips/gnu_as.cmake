# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# GNU as accepts the assembly of every shipped MIPS profile without warnings
# and encodes each instruction as llvm-mc does when `cc -c` assembles the same
# file. The instruction words of the code sections and their $gp-relative
# relocations are compared; labels, symbol tables, and metadata may differ.
# VARIANTS replaces the default "name|flags" compilation variants.

foreach(required CC SOURCES OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

find_program(GNU_AS NAMES mips64-elf-as)
find_program(GNU_OBJDUMP NAMES mips64-elf-objdump)
find_program(LLVM_READOBJ NAMES llvm-readobj)
if(NOT GNU_AS OR NOT GNU_OBJDUMP OR NOT LLVM_READOBJ)
    message(STATUS
        "skipping GNU as acceptance: mips64-elf-as, mips64-elf-objdump, and llvm-readobj are required")
    return()
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN}
                    RESULT_VARIABLE status
                    OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

# Every profile line names its triple and ABI; its m.arch follows indented.
execute_process(COMMAND "${CC}" --print-profiles
                RESULT_VARIABLE status OUTPUT_VARIABLE listing)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc --print-profiles failed")
endif()
string(REPLACE "\n" ";" lines "${listing}")
set(profiles "")
set(current "")
foreach(line IN LISTS lines)
    if(line MATCHES "^([^ ]+)  target: (mips[^ ]*)  ")
        set(current "${CMAKE_MATCH_1}")
        list(APPEND profiles "${current}")
        set(triple_${current} "${CMAKE_MATCH_2}")
        set(arch_${current} mips1)
    elseif(line MATCHES "^[^ ]")
        set(current "")
    elseif(NOT current STREQUAL "" AND line MATCHES "^ +m\\.arch = ([^ ]+)")
        if(NOT CMAKE_MATCH_1 STREQUAL "generic")
            set(arch_${current} "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()
list(LENGTH profiles profile_count)
if(profile_count EQUAL 0)
    message(FATAL_ERROR "cc --print-profiles lists no MIPS profile\n${listing}")
endif()

# The instruction words of each code section, as "section: words" lines.
function(code_words object result)
    execute_process(
        COMMAND "${GNU_OBJDUMP}" -d -M gpr-names=numeric,fpr-names=numeric
                "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot disassemble ${object}\n${error}")
    endif()
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" text_lines "${text}")
    set(words "")
    foreach(text_line IN LISTS text_lines)
        if(text_line MATCHES "^Disassembly of section ([^:]+):")
            string(APPEND words "\n${CMAKE_MATCH_1}:")
        elseif(text_line MATCHES "^ *[0-9a-f]+:\t([0-9a-f]+)[ \t]")
            string(APPEND words " ${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${result} "${words}" PARENT_SCOPE)
endfunction()

# The $gp-relative relocations, as "section: offset type symbol" lines.
function(gp_relocations object result)
    execute_process(COMMAND "${GNU_OBJDUMP}" -r "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot read the relocations of ${object}\n${error}")
    endif()
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" text_lines "${text}")
    set(relocations "")
    foreach(text_line IN LISTS text_lines)
        if(text_line MATCHES "^RELOCATION RECORDS FOR [[]([^]]+)[]]:")
            set(section "${CMAKE_MATCH_1}")
        elseif(text_line MATCHES "^([0-9a-f]+) +(R_MIPS_GPREL[A-Z0-9_]*)[^ ]* +([^ ]+)")
            string(APPEND relocations "\n${section}: ${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3}")
        endif()
    endforeach()
    set(${result} "${relocations}" PARENT_SCOPE)
endfunction()

if(DEFINED VARIANTS)
    set(variants "${VARIANTS}")
else()
    set(variants "O0|-O0" "O2|-O2 -g -ffunction-sections -fdata-sections")
endif()
set(compared 0)
foreach(profile IN LISTS profiles)
    set(triple "${triple_${profile}}")
    if(triple MATCHES "^mips(64)?el-|^mipsallegrexel-")
        set(endian -EL)
    else()
        set(endian -EB)
    endif()
    foreach(source IN LISTS SOURCES)
        get_filename_component(stem "${source}" NAME_WE)
        foreach(variant IN LISTS variants)
            string(REPLACE "|" ";" fields "${variant}")
            list(GET fields 0 level)
            list(GET fields 1 flags)
            separate_arguments(flags NATIVE_COMMAND "${flags}")
            set(base "${OUTPUT}.${profile}.${stem}.${level}")
            run_checked("${profile} ${stem} ${level} assembly"
                        "${CC}" -S -mprofile=${profile} ${flags} "${source}"
                        -o "${base}.s")
            run_checked("${profile} ${stem} ${level} object"
                        "${CC}" -c -mprofile=${profile} ${flags} "${source}"
                        -o "${base}.llvm.o")
            # The object writer's ABI class selects the GNU ABI option.
            execute_process(
                COMMAND "${LLVM_READOBJ}" --file-headers "${base}.llvm.o"
                RESULT_VARIABLE status OUTPUT_VARIABLE header)
            if(header MATCHES "Class: 64-bit")
                set(abi -mabi=64)
            elseif(header MATCHES "EF_MIPS_ABI_EABI32")
                set(abi -mabi=eabi -mgp32)
            else()
                set(abi -mabi=32)
            endif()
            execute_process(
                COMMAND "${GNU_AS}" ${endian} -march=${arch_${profile}} ${abi}
                        --fatal-warnings "${base}.s" -o "${base}.gnu.o"
                RESULT_VARIABLE status
                OUTPUT_VARIABLE stdout
                ERROR_VARIABLE stderr)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR
                    "GNU as rejected ${profile} ${stem} ${level} "
                    "(${endian} -march=${arch_${profile}} ${abi})\n${stdout}\n${stderr}")
            endif()
            code_words("${base}.gnu.o" gnu_words)
            code_words("${base}.llvm.o" llvm_words)
            if(NOT gnu_words STREQUAL llvm_words)
                message(FATAL_ERROR
                    "GNU as and llvm-mc encode ${profile} ${stem} ${level} "
                    "differently\nGNU as:${gnu_words}\nllvm-mc:${llvm_words}")
            endif()
            gp_relocations("${base}.gnu.o" gnu_relocations)
            gp_relocations("${base}.llvm.o" llvm_relocations)
            if(NOT gnu_relocations STREQUAL llvm_relocations)
                message(FATAL_ERROR
                    "GNU as and llvm-mc relocate ${profile} ${stem} ${level} "
                    "differently\nGNU as:${gnu_relocations}\nllvm-mc:${llvm_relocations}")
            endif()
            math(EXPR compared "${compared} + 1")
        endforeach()
    endforeach()
endforeach()
message(STATUS "GNU as matched llvm-mc on ${compared} assemblies")
