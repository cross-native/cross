# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Compile every ```x example in README.md and doc/*.md. Files named by
# $::embed are supplied as small stand-ins.
foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

file(GLOB documents "${SOURCE_DIR}/README.md" "${SOURCE_DIR}/doc/*.md")
set(total 0)
foreach(document IN LISTS documents)
    get_filename_component(name "${document}" NAME_WE)
    file(READ "${document}" text)
    set(index 0)
    while(TRUE)
        string(FIND "${text}" "```x\n" start)
        if(start EQUAL -1)
            break()
        endif()
        math(EXPR start "${start} + 5")
        string(SUBSTRING "${text}" ${start} -1 text)
        string(FIND "${text}" "\n```" stop)
        string(SUBSTRING "${text}" 0 ${stop} example)
        math(EXPR stop "${stop} + 4")
        string(SUBSTRING "${text}" ${stop} -1 text)
        math(EXPR index "${index} + 1")
        math(EXPR total "${total} + 1")

        string(REGEX MATCHALL "\\$::embed\\(\"[^\"]+\"\\)" embeds "${example}")
        foreach(embed IN LISTS embeds)
            string(REGEX REPLACE "^\\$::embed\\(\"([^\"]+)\"\\)$" "\\1" asset "${embed}")
            file(WRITE "${OUTPUT}/${asset}" "example\n")
        endforeach()

        set(input "${OUTPUT}/${name}-${index}.x")
        file(WRITE "${input}" "${example}\n")
        execute_process(
            COMMAND "${CC}" -S -O2 -target x86_64-unknown-linux-gnu
                    "${input}" -o "${OUTPUT}/${name}-${index}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(SEND_ERROR "${name}.md example ${index} failed\n${out}${err}")
        endif()
    endwhile()
endforeach()
if(total EQUAL 0)
    message(FATAL_ERROR "no documented examples found")
endif()
message(STATUS "compiled ${total} documented examples")
