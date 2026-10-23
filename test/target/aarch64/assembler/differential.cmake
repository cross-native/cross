# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `as` assembles SOURCE into the object llvm-mc writes: the same sections,
# relocations, and symbols (compare.py), and the same llvm-objdump
# disassembly. OPTIONS are as's options after the triple, MC_OPTIONS
# llvm-mc's. With aarch64-linux-gnu-as installed, GNU as with GNU_OPTIONS
# must accept SOURCE and GNU_SOURCES, and with GNU_TEXT its .text must equal
# that of as for the same sources. With CC, the sources must hold one
# statement per form of the shipped instruction table.

foreach(required AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
find_program(PYTHON NAMES python3 python)
if(NOT PYTHON)
    message(STATUS "skipping: python is required")
    return()
endif()
find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_OBJDUMP NAMES llvm-objdump)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
if(NOT LLVM_MC OR NOT LLVM_OBJDUMP OR NOT LLVM_OBJCOPY)
    message(STATUS "skipping: llvm-mc, llvm-objdump, and llvm-objcopy are required")
    return()
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
separate_arguments(options UNIX_COMMAND "${OPTIONS}")
separate_arguments(mc_options UNIX_COMMAND "${MC_OPTIONS}")
separate_arguments(gnu_options UNIX_COMMAND "${GNU_OPTIONS}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out
                    ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${out}${err}")
    endif()
endfunction()

# The disassembly of an object without its file name.
function(disassembly object result)
    execute_process(COMMAND "${LLVM_OBJDUMP}" -d "${object}" OUTPUT_VARIABLE text
                    RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot disassemble ${object}")
    endif()
    string(REGEX REPLACE "^[^\n]*\n[^\n]*file format[^\n]*\n" "" text "${text}")
    set(${result} "${text}" PARENT_SCOPE)
endfunction()

set(cross "${OUTPUT}/cross.o")
set(reference "${OUTPUT}/llvm-mc.o")
run_checked("as" "${AS}" -target aarch64-none-elf ${options} "${SOURCE}" -o "${cross}")
run_checked("llvm-mc" "${LLVM_MC}" --triple=aarch64 --filetype=obj ${mc_options} "${SOURCE}"
            -o "${reference}")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare.py" "${cross}" "${reference}"
                RESULT_VARIABLE status OUTPUT_VARIABLE differences)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "the object of ${SOURCE} differs from llvm-mc's:\n${differences}")
endif()
disassembly("${cross}" cross_text)
disassembly("${reference}" reference_text)
if(NOT cross_text STREQUAL reference_text)
    file(WRITE "${OUTPUT}/cross.txt" "${cross_text}")
    file(WRITE "${OUTPUT}/llvm-mc.txt" "${reference_text}")
    message(FATAL_ERROR "the disassembly of ${SOURCE} differs from llvm-mc's; see ${OUTPUT}")
endif()

set(sources "${SOURCE}" ${GNU_SOURCES})
find_program(GNU_AS NAMES aarch64-linux-gnu-as aarch64-none-elf-as)
if(GNU_AS)
    run_checked("GNU as" "${GNU_AS}" ${gnu_options} ${sources} -o "${OUTPUT}/gnu.o")
endif()
if(GNU_AS AND GNU_TEXT)
    run_checked("as of the GNU sources" "${AS}" -target aarch64-none-elf ${options} ${sources}
                -o "${OUTPUT}/cross_gnu.o")
    foreach(object gnu cross_gnu)
        run_checked("extract .text" "${LLVM_OBJCOPY}" -O binary --only-section=.text
                    "${OUTPUT}/${object}.o" "${OUTPUT}/${object}.text")
        file(READ "${OUTPUT}/${object}.text" ${object}_bytes HEX)
    endforeach()
    if(NOT gnu_bytes STREQUAL cross_gnu_bytes)
        message(FATAL_ERROR "the .text of ${sources} differs from GNU as's")
    endif()
endif()

if(DEFINED CC)
    # One statement per form: the sources' statements, with their entry
    # names, against the listing of the shipped forms.
    execute_process(COMMAND "${CC}" -target aarch64-none-elf --print-instructions
                    OUTPUT_VARIABLE listing RESULT_VARIABLE status)
    string(REGEX MATCHALL "source: shipped:aarch64_instructions[.]xm:[0-9]+" forms "${listing}")
    list(LENGTH forms form_count)
    set(names "")
    foreach(source IN LISTS sources)
        file(STRINGS "${source}" lines REGEX "^\t[a-z].* // ")
        foreach(line IN LISTS lines)
            string(REGEX REPLACE "^.* // " "" name "${line}")
            list(APPEND names "${name}")
        endforeach()
    endforeach()
    list(LENGTH names name_count)
    list(REMOVE_DUPLICATES names)
    list(LENGTH names unique_count)
    if(NOT name_count EQUAL form_count OR NOT unique_count EQUAL form_count)
        message(FATAL_ERROR "the sources hold ${name_count} statements for ${unique_count} "
                            "entries; the shipped table has ${form_count} forms")
    endif()
    message(STATUS "${form_count} forms matched")
endif()
