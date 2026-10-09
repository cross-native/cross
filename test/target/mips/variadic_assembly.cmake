# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks the prologues and state addresses of the variadic definitions in
# variadic_assembly.x and the count register of a variadic_count.xm call.
foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -S -O2 "--model=${MODEL}"
                        -target mips-unknown-elf -march=vr4300
                        "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "compilation failed\n${errors}")
endif()
file(READ "${OUTPUT}.s" text)

function(body name variable)
    string(FIND "${text}" "\n${name}:\n" begin)
    string(FIND "${text}" "\n.end ${name}\n" end)
    if(begin EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no assembly for ${name}\n${text}")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} result)
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(expect name code pattern)
    if(NOT code MATCHES "${pattern}")
        message(FATAL_ERROR "${name} lacks '${pattern}'\n${code}")
    endif()
endfunction()

# Each binding definition homes a0-a3 in the caller's area at $sp + frame and
# points arg_area just past the named slots.
foreach(case "o32_leaf|0|4" "o32_after_double|0|8" "o32_forward||4")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 frame)
    list(GET parts 2 state)
    body(${name} code)
    if(frame STREQUAL "")
        if(NOT code MATCHES "addiu\t[$]sp,[$]sp,-([0-9]+)\n")
            message(FATAL_ERROR "${name} has no frame\n${code}")
        endif()
        set(frame ${CMAKE_MATCH_1})
    endif()
    foreach(index 0 1 2 3)
        math(EXPR offset "${frame} + 4 * ${index}")
        expect(${name} "${code}" "\tsw\t[$]a${index},${offset}[(][$]sp[)]\n")
    endforeach()
    math(EXPR offset "${frame} + ${state}")
    expect(${name} "${code}" "\taddiu\t[$][a-z0-9]+,[$]sp,${offset}\n")
endforeach()

# The state escapes to the worker, so the call keeps this frame.
body(o32_forward code)
expect(o32_forward "${code}" "\tjal\to32_worker\n")

body(o32_ignore code)
if(code MATCHES "\tsw\t[$]a[0-3],")
    message(FATAL_ERROR "o32_ignore homes registers without bindings\n${code}")
endif()

# cross32 saves a0-a3, t0-t9, and s0-s7, then the eight floating argument
# registers, in one area of the frame.
body(cross32_leaf code)
if(NOT code MATCHES "\tsw\t[$]a0,([0-9]+)[(][$]sp[)]\n")
    message(FATAL_ERROR "cross32_leaf does not save a0\n${code}")
endif()
set(area ${CMAKE_MATCH_1})
foreach(entry "sw|s7|84" "sdc1|f12|88" "sdc1|f10|144")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 opcode)
    list(GET parts 1 reg)
    list(GET parts 2 displacement)
    math(EXPR offset "${area} + ${displacement}")
    expect(cross32_leaf "${code}" "\t${opcode}\t[$]${reg},${offset}[(][$]sp[)]\n")
endforeach()
foreach(displacement 4 88)
    math(EXPR offset "${area} + ${displacement}")
    expect(cross32_leaf "${code}" "\taddiu\t[$][a-z0-9]+,[$]sp,${offset}\n")
endforeach()

body(call_counted code)
expect(call_counted "${code}" "\tli\t[$]v1,3\n\tjal\tcounted\n")
