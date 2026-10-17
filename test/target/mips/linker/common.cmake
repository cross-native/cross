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

# same_as_gnu(LABEL CROSS_IMAGE GNU_IMAGE): the images have the same section
# and program headers (section file offsets aside), symbols (in any order),
# and allocated contents.
function(same_as_gnu label cross gnu)
    foreach(image cross gnu)
        run_checked("${label} ${image} headers" "${LLVM_READELF}" -S -l -W "${${image}}")
        string(REGEX REPLACE "There are [0-9]+ section headers, starting at offset 0x[0-9a-f]+" "" text "${run_stdout}")
        string(REGEX REPLACE "(\n +\\[ *[0-9]+\\] [^ \n]* +[A-Z_0-9]+ +[0-9a-f]+) [0-9a-f]+" "\\1" text "${text}")
        # The string tables' sizes depend on how they share strings.
        string(REGEX REPLACE "\n +\\[ *[0-9]+\\] [.](sym|str|shstr)tab [^\n]*" "" text "${text}")
        set(${image}_headers "${text}")
        run_checked("${label} ${image} symbols" "${LLVM_READELF}" -s -W "${${image}}")
        string(REGEX MATCHALL "\n +[0-9]+: [^\n]*" rows "${run_stdout}")
        list(TRANSFORM rows REPLACE "\n +[0-9]+: " "")
        list(SORT rows)
        set(${image}_symbols "${rows}")
    endforeach()
    if(NOT cross_headers STREQUAL gnu_headers)
        message(FATAL_ERROR "${label}: headers differ from GNU ld's\n${cross_headers}\n${gnu_headers}")
    endif()
    if(NOT cross_symbols STREQUAL gnu_symbols)
        message(FATAL_ERROR "${label}: symbols differ from GNU ld's\n${cross_symbols}\n${gnu_symbols}")
    endif()
    string(REGEX MATCHALL "\n +\\[ *[0-9]+\\] [^ \n]+ +PROGBITS +[0-9a-f]+ [0-9a-f]+ [0-9a-f]+ [0-9a-f]+ +[A-Z]*A"
           lines "${cross_headers}")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "\n +\\[ *[0-9]+\\] ([^ ]+) .*" "\\1" name "${line}")
        section_hex("${cross}" "${name}" cross_bytes)
        section_hex("${gnu}" "${name}" gnu_bytes)
        if(NOT cross_bytes STREQUAL gnu_bytes)
            message(FATAL_ERROR "${label}: ${name} differs from GNU ld's\n${cross_bytes}\n${gnu_bytes}")
        endif()
    endforeach()
endfunction()
