# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A function-like macro invocation ends at its matching parenthesis, even
# when its arguments or its opening parenthesis are on later lines.
foreach(required CPP CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(COMMAND "${CPP}" "${SOURCE}" -o "${OUTPUT}.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.i" expanded)
foreach(pattern "TWICE" "PICK" "NAMED" "has_include")
    if(expanded MATCHES "${pattern}")
        message(FATAL_ERROR "invocation of ${pattern} was not expanded\n${expanded}")
    endif()
endforeach()
if(NOT expanded MATCHES "after_spans = 17;" OR
   NOT expanded MATCHES "#line 17 \"[^\"]*macro_lines[.]x\"")
    message(FATAL_ERROR "lines after the invocations lost their positions\n${expanded}")
endif()

execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${SOURCE}"
    -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.s" assembly)
foreach(pair "spanning;14" "nested;10" "parenthesis_below;6" "queried;1"
             "after_spans;17")
    list(GET pair 0 name)
    list(GET pair 1 value)
    if(NOT assembly MATCHES "\n${name}:\n\t[.]long ${value}\n")
        message(FATAL_ERROR "${name} is not ${value}\n${assembly}")
    endif()
endforeach()

function(reject name source)
    file(WRITE "${OUTPUT}.${name}.x" "${source}")
    execute_process(COMMAND "${CPP}" "${OUTPUT}.${name}.x" -o "${OUTPUT}.${name}.i"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES
       "${name}[.]x:2:[0-9]+: error: unterminated argument list invoking macro 'TWICE'")
        message(FATAL_ERROR "${name} was accepted\n${out}\n${err}")
    endif()
endfunction()
reject(end_of_input "#define TWICE(x) ((x) + (x))\nglobal i32 value = TWICE(1 +\n")
reject(directive "#define TWICE(x) ((x) + (x))\nglobal i32 value = TWICE(1 +\n#define OTHER 2\n2);\n")
