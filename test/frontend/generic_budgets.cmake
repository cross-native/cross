# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -fgeneric-depth-limit and -fgeneric-instance-limit bound instantiation.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(WRITE "${OUTPUT}.depth.x"
    "static i32 third<i32 N>(in i32 x) { return x + N; }\n"
    "static i32 second<i32 N>(in i32 x) { return third::<N>(x) * 2; }\n"
    "static i32 first<i32 N>(in i32 x) { return second::<N>(x) + 1; }\n"
    "global i32 depth(in i32 x) { return first::<7>(x); }\n")
file(WRITE "${OUTPUT}.count.x"
    "static i32 add<i32 N>(in i32 x) { return x + N; }\n"
    "global i32 count(in i32 x) { return add::<1>(x) + add::<2>(x) + add::<3>(x); }\n")

function(compile name option expected)
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${option}
        "${OUTPUT}.${name}.x" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expected STREQUAL "accept")
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${name} ${option} was rejected\n${out}\n${err}")
        endif()
    elseif(status EQUAL 0 OR NOT err MATCHES "${name}[.]x:[0-9]+:[0-9]+: error: ${expected}")
        message(FATAL_ERROR "${name} ${option} lacks '${expected}'\n${out}\n${err}")
    endif()
endfunction()

compile(depth "" accept)
compile(depth -fgeneric-depth-limit=3 accept)
compile(depth -fgeneric-depth-limit=2
    "generic instantiation budget exceeded: nesting depth limit 2 [(]-fgeneric-depth-limit[)]")
compile(count -fgeneric-instance-limit=3 accept)
compile(count -fgeneric-instance-limit=2
    "generic instantiation budget exceeded: instance limit 2 [(]-fgeneric-instance-limit[)]")

execute_process(COMMAND "${CC}" -S -fgeneric-depth-limit=0 "${OUTPUT}.depth.x"
    -o "${OUTPUT}.zero.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "-fgeneric-depth-limit.*must be between 1 and")
    message(FATAL_ERROR "a zero depth limit was accepted\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" --print-options=common -fgeneric-instance-limit=9
    RESULT_VARIABLE status OUTPUT_VARIABLE listing ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR
   NOT listing MATCHES "f[.]generic-instance-limit  type: unsigned[^\n]*value: 9  origin: command-line" OR
   NOT listing MATCHES "f[.]generic-depth-limit  type: unsigned[^\n]*value: 128  origin: default")
    message(FATAL_ERROR "generic budgets are not listed\n${listing}\n${err}")
endif()
