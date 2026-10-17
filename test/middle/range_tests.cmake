# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Two tests of one integer that pass for one range of values compile to one
# comparison; tests that pass for two separate values keep both.

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
    message(FATAL_ERROR "range test compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)

function(comparisons function expected)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    string(REGEX MATCHALL "\t(cmp|test)[bwlq]\t" found "${text}")
    list(LENGTH found count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${function} has ${count} comparisons, not ${expected}\n${text}")
    endif()
endfunction()

foreach(function outside inside mirrored pair neither ends three shifted
                 around_sign extremes)
    comparisons(${function} 1)
endforeach()
comparisons(gap 2)
comparisons(everything 0)
comparisons(nothing 0)
