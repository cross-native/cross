# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under register pressure the allocator leaves long ranges without a color
# rather than the one-instruction temporaries between them: in ring22 at
# -O2 (unrolled by two) and ring30 at -Oz, no shift that computes x * 3 as
# (x << 2) - x stores its result to the frame, and each function makes at
# most 160 frame accesses.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile name)
    execute_process(
        COMMAND "${CC}" -S -fno-eval-calls -mprofile=vr4300-o32 ${ARGN}
                "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(function_body output symbol result)
    file(READ "${output}" assembly)
    string(FIND "${assembly}" "\n${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${output} does not define ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${result} "${body}" PARENT_SCOPE)
endfunction()

foreach(case "O2|ring22|160" "Oz|ring30|160")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 level)
    list(GET fields 1 symbol)
    list(GET fields 2 limit)
    compile(${level} -${level})
    function_body("${OUTPUT}.${level}.s" ${symbol} body)
    string(REGEX MATCHALL "\n\t(lw|sw|ld|sd)\t[^\n]*\\(\\$sp\\)" frame "${body}")
    list(LENGTH frame accesses)
    string(REGEX MATCHALL "\n\tsll\t\\$[a-z0-9]+,[^\n]*\n\tsw\t\\$[a-z0-9]+,[0-9]+\\(\\$sp\\)"
           pairs "${body}")
    foreach(pair IN LISTS pairs)
        string(REGEX MATCH "sll\t\\$([a-z0-9]+),[^\n]*\n\tsw\t\\$([a-z0-9]+)," matched "${pair}")
        if(CMAKE_MATCH_1 STREQUAL CMAKE_MATCH_2)
            message(FATAL_ERROR
                "-${level} ${symbol} spills a shift's one-instruction result\n${body}")
        endif()
    endforeach()
    if(accesses GREATER limit)
        message(FATAL_ERROR
            "-${level} ${symbol} makes ${accesses} frame accesses, more than ${limit}\n${body}")
    endif()
endforeach()
