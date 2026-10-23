# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# With aarch64-linux-gnu-as installed, `as` assembles SOURCE, which uses
# GNU as's own syntax, into the .text and relocations that GNU as writes.

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
find_program(GNU_AS NAMES aarch64-linux-gnu-as aarch64-none-elf-as)
if(NOT GNU_AS)
    message(STATUS "skipping: GNU as for AArch64 is not installed")
    return()
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
foreach(command "${AS};-target;aarch64-none-elf" "${GNU_AS}")
    list(GET command 0 tool)
    get_filename_component(stem "${tool}" NAME)
    execute_process(COMMAND ${command} "${SOURCE}" -o "${OUTPUT}/${stem}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${stem} failed\n${err}")
    endif()
    list(APPEND objects "${OUTPUT}/${stem}.o")
endforeach()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare.py" --relocations
                        ${objects}
                RESULT_VARIABLE status OUTPUT_VARIABLE differences)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "the relocations of ${SOURCE} differ from GNU as's:\n${differences}")
endif()
