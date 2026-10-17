# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Helpers shared by the MIPS linker tests.

foreach(required AS LD OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLVM_READELF NAMES llvm-readelf)
find_program(LLVM_NM NAMES llvm-nm)
if(NOT LLVM_OBJCOPY OR NOT LLVM_READELF OR NOT LLVM_NM)
    message(STATUS "skipping linker test: llvm-objcopy, llvm-readelf, and llvm-nm are required")
    set(LINKER_TEST_SKIP TRUE)
    return()
endif()
find_program(GNU_LD NAMES mips64-elf-ld)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    set(run_stdout "${stdout}" PARENT_SCOPE)
    set(run_stderr "${stderr}" PARENT_SCOPE)
endfunction()

# run_failing(LABEL COMMAND...): the command fails; its standard error is
# returned in run_stderr.
function(run_failing label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "${label} succeeded unexpectedly\n${stdout}\n${stderr}")
    endif()
    set(run_stderr "${stderr}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# The bytes of an executable's section as lowercase hexadecimal.
function(section_hex image section result)
    run_checked("extract ${section}" "${LLVM_OBJCOPY}" -O binary --only-section=${section}
                "${image}" "${image}${section}.bin")
    file(READ "${image}${section}.bin" hex HEX)
    set(${result} "${hex}" PARENT_SCOPE)
endfunction()

# The four-byte words of an executable's section, in the given byte order.
function(section_words image section order result)
    section_hex("${image}" "${section}" hex)
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

# The value of a symbol in an image, as llvm-nm prints it (hexadecimal).
function(symbol_value image name result)
    run_checked("symbols of ${image}" "${LLVM_NM}" --format=posix "${image}")
    string(REGEX MATCH "(^|\n)${name} [A-Za-z] ([0-9a-f]+)" match "${run_stdout}")
    if(NOT match)
        message(FATAL_ERROR "${image} has no symbol ${name}\n${run_stdout}")
    endif()
    set(${result} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
