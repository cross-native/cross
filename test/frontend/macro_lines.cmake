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
             "after_spans;17" "case_one;20" "case_two;30" "case_other;2"
             "split_size;4")
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

# Diagnostics name written columns: tokens after a macro, a comment, or a line
# splice keep their own, and an expansion's tokens take the invocation's.
get_filename_component(directory "${SOURCE}" DIRECTORY)
execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
    "${directory}/macro_columns.x" -o "${OUTPUT}.columns.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
foreach(location "10:36: error: record has no member named 'first'"
                 "13:46: error: unresolved name 'second'"
                 "17:24: error: record has no member named 'third'"
                 "21:6: error: record has no member named 'fourth'")
    if(status EQUAL 0 OR NOT err MATCHES "macro_columns[.]x:${location}")
        message(FATAL_ERROR "diagnostic not at macro_columns.x:${location}\n${out}\n${err}")
    endif()
endforeach()
file(WRITE "${OUTPUT}.expansion.x"
    "#define BAD (1 +)\nglobal i32 value() { return 2 * BAD; }\n")
execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
    "${OUTPUT}.expansion.x" -o "${OUTPUT}.expansion.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "expansion[.]x:2:33: error: expected expression")
    message(FATAL_ERROR "an expansion's token is not at its invocation\n${out}\n${err}")
endif()
