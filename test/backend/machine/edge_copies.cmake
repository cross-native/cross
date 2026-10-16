# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A phi copy that both edges of a conditional branch perform runs once before
# the branch: no edge label starts with the copy that the branch's
# fallthrough path performs.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
execute_process(
    COMMAND "${CC}" -S -O2 -fno-eval-calls -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "edge-copy compilation failed\n${stdout}\n${stderr}")
endif()
file(STRINGS "${OUTPUT}.s" lines)
list(LENGTH lines count)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    list(GET lines ${index} line)
    if(NOT line MATCHES "^\tj[a-z]+\t([.]L[^ ]*[.](true|false)[.]edge[.][0-9]+)$")
        continue()
    endif()
    set(label "${CMAKE_MATCH_1}")
    math(EXPR next "${index} + 1")
    list(GET lines ${next} fallthrough)
    list(FIND lines "${label}:" label_index)
    if(label_index EQUAL -1)
        continue()
    endif()
    math(EXPR first "${label_index} + 1")
    list(GET lines ${first} edge)
    if(fallthrough MATCHES "^\tmov" AND fallthrough STREQUAL edge)
        message(FATAL_ERROR
            "both edges of the branch to ${label} copy: ${edge}\n"
            "see ${OUTPUT}.s")
    endif()
endforeach()
