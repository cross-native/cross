# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CPP CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}/include")
file(WRITE "${OUTPUT}/forced.x" "#define FORCED 7\nglobal u32 forced_object = FORCED;\n")
file(WRITE "${OUTPUT}/include/searched.x" "#define SEARCHED 3\n")
file(WRITE "${OUTPUT}/macros.x"
    "#define LIMIT 5\n#undef FORCED\nglobal u32 discarded_object = LIMIT;\n")
file(WRITE "${OUTPUT}/first.x"
    "#define FIRST_ONLY\nglobal u32 first = FORCED + SEARCHED;\n")
file(WRITE "${OUTPUT}/second.x" "#ifdef FIRST_ONLY\n#error leaked\n#endif\nglobal u32 second = LIMIT;\n")

function(run name)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${OUTPUT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(${name}_status "${status}" PARENT_SCOPE)
    set(${name}_output "${out}${err}" PARENT_SCOPE)
endfunction()

function(expect_contains text)
    foreach(pattern ${ARGN})
        string(FIND "${text}" "${pattern}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "missing '${pattern}' in:\n${text}")
        endif()
    endforeach()
endfunction()

# -include precedes each input and is searched in the current directory, then -I.
run(forced "${CPP}" -I include -include forced.x -include searched.x
    first.x -o forced.i)
if(NOT forced_status EQUAL 0)
    message(FATAL_ERROR "cpp -include failed\n${forced_output}")
endif()
file(READ "${OUTPUT}/forced.i" forced)
expect_contains("${forced}" "global u32 forced_object = 7;" "global u32 first = 7 + 3;")

# -imacros keeps definitions and #undef effects but no output, and runs first.
run(macros "${CPP}" -include forced.x -imacros macros.x first.x second.x
    -I include -include searched.x -o macros.i)
if(NOT macros_status EQUAL 0)
    message(FATAL_ERROR "cpp -imacros failed\n${macros_output}")
endif()
file(READ "${OUTPUT}/macros.i" macros)
expect_contains("${macros}" "global u32 first = 7 + 3;" "global u32 second = 5;")
if(macros MATCHES "discarded_object")
    message(FATAL_ERROR "-imacros emitted source text:\n${macros}")
endif()

# cc accepts the same options, and dependency output lists the forced files.
run(compile "${CC}" -S -target x86_64-unknown-linux-gnu -I include
    -include forced.x -imacros include/searched.x first.x -o first.s)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "cc -include failed\n${compile_output}")
endif()
run(depend "${CPP}" -M -include forced.x -imacros macros.x -I include
    -include searched.x first.x)
expect_contains("${depend_output}" "forced.x" "macros.x" "searched.x")

run(missing "${CPP}" -include absent.x first.x -o missing.i)
if(missing_status EQUAL 0 OR NOT missing_output MATCHES "forced include file not found: absent.x")
    message(FATAL_ERROR "missing forced include was not diagnosed\n${missing_output}")
endif()
run(argument "${CPP}" first.x -imacros)
if(argument_status EQUAL 0 OR NOT argument_output MATCHES "missing argument to '-imacros'")
    message(FATAL_ERROR "missing -imacros operand was not diagnosed\n${argument_output}")
endif()
