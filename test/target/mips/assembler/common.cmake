# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Helpers shared by the MIPS assembler tests.

if(DEFINED OUTPUT)
    get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
endif()
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLVM_READELF NAMES llvm-readelf)
if(NOT LLVM_OBJCOPY OR NOT LLVM_READELF)
    message(STATUS "skipping assembler test: llvm-objcopy and llvm-readelf are required")
    set(ASSEMBLER_TEST_SKIP TRUE)
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

# The four-byte words of an object's section as lowercase hexadecimal, read
# in the object's byte order.
function(section_words object section order result)
    run_checked("extract ${section}" "${LLVM_OBJCOPY}"
                --dump-section=${section}=${object}${section}.bin "${object}"
                "${object}.copy")
    file(READ "${object}${section}.bin" hex HEX)
    string(LENGTH "${hex}" length)
    set(words "")
    set(index 0)
    while(index LESS length)
        string(SUBSTRING "${hex}" ${index} 8 word)
        if(order STREQUAL "little")
            string(SUBSTRING "${word}" 0 2 b0)
            string(SUBSTRING "${word}" 2 2 b1)
            string(SUBSTRING "${word}" 4 2 b2)
            string(SUBSTRING "${word}" 6 2 b3)
            set(word "${b3}${b2}${b1}${b0}")
        endif()
        list(APPEND words "${word}")
        math(EXPR index "${index} + 8")
    endwhile()
    set(${result} "${words}" PARENT_SCOPE)
endfunction()

function(text_words object order result)
    section_words("${object}" .text ${order} words)
    set(${result} "${words}" PARENT_SCOPE)
endfunction()

# The words an assembly file's instruction comments list, in order.
function(commented_words source result)
    file(STRINGS "${source}" lines)
    set(words "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]+[a-z].*#[ ]*([0-9a-f ]+)$")
            string(REPLACE " " ";" each "${CMAKE_MATCH_1}")
            list(APPEND words ${each})
        endif()
    endforeach()
    set(${result} "${words}" PARENT_SCOPE)
endfunction()

# Relocations as "section offset type symbol[ + addend]" lines.
function(relocation_lines object result)
    execute_process(COMMAND "${LLVM_READELF}" -r --wide "${object}"
                    RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot read the relocations of ${object}\n${error}")
    endif()
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" text_lines "${text}")
    set(lines "")
    foreach(line IN LISTS text_lines)
        if(line MATCHES "^Relocation section '([^']+)'")
            set(section "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^ *([0-9a-f]+) +[0-9a-f]+ +(R_MIPS_[A-Z0-9_/]+) +[0-9a-f]+ +(.*)$")
            set(offset "${CMAKE_MATCH_1}")
            set(type "${CMAKE_MATCH_2}")
            set(target "${CMAKE_MATCH_3}")
            string(REGEX REPLACE "^0+(.)" "\\1" offset "${offset}")
            string(REPLACE "/R_MIPS_NONE" "" type "${type}")
            list(APPEND lines "${section} ${offset} ${type} ${target}")
        endif()
    endforeach()
    set(${result} "${lines}" PARENT_SCOPE)
endfunction()

function(expect_lines label actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        string(REPLACE ";" "\n  " shown_actual "${actual}")
        string(REPLACE ";" "\n  " shown_expected "${expected}")
        message(FATAL_ERROR "${label} differ\nexpected:\n  ${shown_expected}\nactual:\n  ${shown_actual}")
    endif()
endfunction()
