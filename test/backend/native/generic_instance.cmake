# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each compilation group emits pick::<u32>, an instance of a global generic,
# as a mergeable definition: a COMDAT group on ELF and COFF, a weak definition
# on Mach-O. ELF groups are also linked to show that the duplicate is dropped.

foreach(required CC FIRST SECOND OUTPUT TARGETS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
find_program(LLD NAMES ld.lld REQUIRED)

function(run_checked label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    set(run_output "${stdout}" PARENT_SCOPE)
endfunction()

foreach(target IN LISTS TARGETS)
    set(objects)
    foreach(source FIRST SECOND)
        set(object "${OUTPUT}-${target}-${source}.o")
        run_checked("${target} ${source} object"
            "${CC}" -c -O2 -target "${target}" "${${source}}" -o "${object}")
        list(APPEND objects "${object}")
    endforeach()
    list(GET objects 0 first_object)
    if(target MATCHES "apple")
        run_checked("${target} symbols" "${LLVM_READOBJ}" --symbols
                    "${first_object}")
        if(NOT run_output MATCHES
           "Name: _pick::<u32>[^}]*WeakDef")
            message(FATAL_ERROR
                "${target} instance is not a weak definition\n${run_output}")
        endif()
    elseif(target MATCHES "windows")
        run_checked("${target} symbols" "${LLVM_READOBJ}" --symbols
                    "${first_object}")
        if(NOT run_output MATCHES
               "Name: [.]text[$]pick::<u32>[^}]*Selection: Any" OR
           NOT run_output MATCHES
               "Name: [.]rdata[$]pick::<u32>[^}]*Selection: Associative")
            message(FATAL_ERROR
                "${target} instance or jump table is not a COMDAT\n${run_output}")
        endif()
    else()
        run_checked("${target} groups" "${LLVM_READOBJ}" --section-groups
                    "${first_object}")
        if(NOT run_output MATCHES
               "Type: COMDAT[^}]*Signature: pick::<u32>[^}]*[.]text[.]pick::<u32>")
            message(FATAL_ERROR
                "${target} instance is not in a COMDAT group\n${run_output}")
        endif()
        if(target MATCHES "^x86_64" AND NOT run_output MATCHES
               "Signature: pick::<u32>[^}]*[.]rodata[.]pick::<u32>")
            message(FATAL_ERROR
                "${target} jump table is not in the instance group\n${run_output}")
        endif()
        if(target MATCHES "^x86_64")
            set(emulation elf_x86_64)
        elseif(target MATCHES "^mips64el")
            set(emulation elf64ltsmip)
        elseif(target MATCHES "^mips64")
            set(emulation elf64btsmip)
        elseif(target MATCHES "^mipsel")
            set(emulation elf32ltsmip)
        else()
            set(emulation elf32btsmip)
        endif()
        run_checked("${target} link" "${LLD}" -m ${emulation}
                    -e generic_instance_entry ${objects}
                    -o "${OUTPUT}-${target}.elf")
    endif()
endforeach()
