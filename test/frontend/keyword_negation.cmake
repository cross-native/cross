# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# `return !(x)` is a negation whether or not the unit defines macros.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
    foreach(macros plain WITH_MACRO)
        set(stem "${OUTPUT}-${target}-${macros}")
        set(defines)
        if(macros STREQUAL WITH_MACRO)
            set(defines -DWITH_MACRO)
        endif()
        execute_process(COMMAND "${CC}" -target ${target} ${defines} -S "${SOURCE}" -o "${stem}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "keyword negation rejected (${target}/${macros})\n${out}\n${err}")
        endif()
        file(READ "${stem}.s" assembly)
        if(NOT assembly MATCHES "\neven_two:\n\t[.]byte 1\n" OR
           NOT assembly MATCHES "\neven_three:\n\t[.]byte 0\n")
            message(FATAL_ERROR "keyword negation has the wrong value (${target}/${macros})\n${assembly}")
        endif()
    endforeach()
endforeach()
