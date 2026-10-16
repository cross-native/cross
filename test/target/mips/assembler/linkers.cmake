# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# GNU ld and LLD accept the `cc -c` objects of every shipped MIPS profile,
# both in a relocatable link and in a final link at a KSEG0 address, and
# GNU objdump disassembles every word of their code.

foreach(required CC SOURCES OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(GNU_LD NAMES mips64-elf-ld)
find_program(GNU_OBJDUMP NAMES mips64-elf-objdump)
find_program(LLD NAMES ld.lld)
find_program(LLVM_READOBJ NAMES llvm-readobj)
find_program(LLVM_READELF NAMES llvm-readelf)
if(NOT GNU_LD OR NOT GNU_OBJDUMP OR NOT LLD OR NOT LLVM_READOBJ OR NOT LLVM_READELF)
    message(STATUS
        "skipping MIPS linker acceptance: mips64-elf-ld, mips64-elf-objdump, ld.lld, llvm-readobj, and llvm-readelf are required")
    return()
endif()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0 OR NOT stderr STREQUAL "")
        message(FATAL_ERROR "${label} failed or warned\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

execute_process(COMMAND "${CC}" --print-profiles OUTPUT_VARIABLE listing)
string(REGEX MATCHALL "\n([^ \n]+)  target: mips" lines "\n${listing}")
set(profiles "")
foreach(line IN LISTS lines)
    string(REGEX REPLACE "\n([^ ]+)  target: mips" "\\1" profile "${line}")
    list(APPEND profiles "${profile}")
endforeach()
list(LENGTH profiles count)
if(count EQUAL 0)
    message(FATAL_ERROR "cc --print-profiles lists no MIPS profile")
endif()

set(linked 0)
foreach(profile IN LISTS profiles)
    foreach(source IN LISTS SOURCES)
        get_filename_component(stem "${source}" NAME_WE)
        set(base "${OUTPUT}.${profile}.${stem}")
        run_checked("${profile} ${stem}" "${CC}" -c -O2 -g -mprofile=${profile} "${source}"
                    -o "${base}.o")
        execute_process(COMMAND "${LLVM_READOBJ}" --file-headers "${base}.o" OUTPUT_VARIABLE header)
        set(class 32)
        if(header MATCHES "Class: 64-bit")
            set(class 64)
        endif()
        set(order b)
        set(endian -EB)
        if(header MATCHES "DataEncoding: LittleEndian")
            set(order l)
            set(endian -EL)
        endif()
        set(emulation elf${class}${order}tsmip)
        # The embedded GNU ld has one ELF32 emulation and selects ELF64 by
        # output format.
        set(gnu_format "")
        if(class EQUAL 64)
            if(order STREQUAL "b")
                set(gnu_format --oformat=elf64-bigmips)
            else()
                set(gnu_format --oformat=elf64-littlemips)
            endif()
        endif()
        run_checked("GNU ld -r ${profile} ${stem}" "${GNU_LD}" ${endian} ${gnu_format} -r
                    "${base}.o" -o "${base}.gnu-r.o")
        run_checked("LLD -r ${profile} ${stem}" "${LLD}" -m ${emulation} -r "${base}.o"
                    -o "${base}.lld-r.o")
        run_checked("GNU ld ${profile} ${stem}" "${GNU_LD}" ${endian} ${gnu_format}
                    -Ttext=0x80010000 -e 0x80010000 --no-warn-rwx-segments "${base}.o"
                    -o "${base}.gnu.elf")
        run_checked("LLD ${profile} ${stem}" "${LLD}" -m ${emulation} -Ttext=0x80010000
                    -e 0x80010000 "${base}.o" -o "${base}.lld.elf")
        run_checked("GNU objdump ${profile} ${stem}" "${GNU_OBJDUMP}" -d -z "${base}.o")
        string(REGEX MATCHALL "\n *[0-9a-f]+:\t[0-9a-f]+ " words "${stdout}")
        list(LENGTH words disassembled)
        execute_process(COMMAND "${LLVM_READELF}" -S --wide "${base}.o" OUTPUT_VARIABLE sections)
        string(REGEX MATCHALL "\\.text[^ ]* +PROGBITS +[0-9a-f]+ +[0-9a-f]+ +[0-9a-f]+" texts "${sections}")
        set(text_bytes 0)
        foreach(text IN LISTS texts)
            string(REGEX MATCH "([0-9a-f]+)$" ignored "${text}")
            math(EXPR text_bytes "${text_bytes} + 0x${CMAKE_MATCH_1}" OUTPUT_FORMAT DECIMAL)
        endforeach()
        math(EXPR text_words "${text_bytes} / 4")
        if(NOT disassembled EQUAL text_words)
            message(FATAL_ERROR "GNU objdump decoded ${disassembled} of ${text_words} words of ${base}.o")
        endif()
        math(EXPR linked "${linked} + 1")
    endforeach()
endforeach()
message(STATUS "GNU ld and LLD linked ${linked} objects")
