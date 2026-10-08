# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Deeply nested blocks, long operator chains, and deep parentheses compile
# instead of exhausting the compiler's stack.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${input}"
                            -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} failed (${status})\n${out}\n${err}")
    endif()
endfunction()

string(REPEAT "if (x) {\n" 400 open)
string(REPEAT "}\n" 400 close)
compile(nested_blocks "global u32 f(in u32 x) {\n${open}x += 1u32;\n${close}return x;\n}\n")

string(REPEAT " + x" 3000 chain)
compile(operator_chain "global u32 f(in u32 x) {\n    return x${chain};\n}\n")

string(REPEAT "(" 10000 left)
string(REPEAT ")" 10000 right)
compile(parentheses "global u32 f() {\n    return ${left}1u32${right};\n}\n")
