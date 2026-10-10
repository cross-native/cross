# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under a profile that flushes denormal results and traps the invalid
# operation and denormal operands, an evaluated result flushes to zero, and an
# operation that would trap is not evaluated: an opportunistic evaluation
# leaves it for runtime and a required one is an error. The default profile
# evaluates the same source with gradual underflow.

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S -O2 -target x86_64-unknown-linux-gnu
                            ${ARGN} "${SOURCE}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set(status "${status}" PARENT_SCOPE)
    set(stderr "${stderr}" PARENT_SCOPE)
    if(status EQUAL 0)
        file(READ "${OUTPUT}/${label}.s" assembly)
        set(assembly "${assembly}" PARENT_SCOPE)
    endif()
endfunction()

function(expect_object label object bits)
    if(NOT assembly MATCHES "\n${object}:\n\t[.]long ${bits}\n")
        message(FATAL_ERROR "${label}: ${object} is not ${bits}\n${assembly}")
    endif()
endfunction()

function(function_text result function)
    string(FIND "${assembly}" "\n${function}:\n" start)
    string(FIND "${assembly}" "\n.size ${function}," end)
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no function ${function} in\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    set(${result} "${text}" PARENT_SCOPE)
endfunction()

set(trapping "--model=${MODEL}" -mprofile=trapping-fp)
compile(default)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "the default profile rejected the source\n${stderr}")
endif()
expect_object(default tiny 71362)
expect_object(default negative_tiny 2147555010)
expect_object(default written 71362)
expect_object(default huge 2139095040)
function_text(text invalid_quotient)
if(text MATCHES "\tdivss\t")
    message(FATAL_ERROR "default: 0/0 was not evaluated\n${text}")
endif()

compile(trapping ${trapping})
if(NOT status EQUAL 0)
    message(FATAL_ERROR "the trapping profile rejected the source\n${stderr}")
endif()
expect_object(trapping tiny 0)
expect_object(trapping negative_tiny 2147483648)
expect_object(trapping written 71362)
expect_object(trapping huge 2139095040)
function_text(text folded)
if(text MATCHES "\tdivss\t")
    message(FATAL_ERROR "trapping: 1/4 was not evaluated\n${text}")
endif()
function_text(text invalid_quotient)
if(NOT text MATCHES "\tdivss\t")
    message(FATAL_ERROR "trapping: 0/0 was evaluated\n${text}")
endif()

foreach(case INVALID DENORMAL)
    compile(default-${case} -D${case})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "default: ${case} was rejected\n${stderr}")
    endif()
endforeach()
compile(trapping-invalid -DINVALID ${trapping})
if(status EQUAL 0 OR NOT stderr MATCHES
   "evaluation[.]x:22:[0-9]+: error: floating-point operation raises the enabled 'invalid' exception during translation-time evaluation")
    message(FATAL_ERROR "trapping: $::eval(0/0) was not diagnosed\n${stderr}")
endif()
compile(trapping-denormal -DDENORMAL ${trapping})
if(status EQUAL 0 OR NOT stderr MATCHES
   "evaluation[.]x:24:[0-9]+: error: floating-point operation traps on a denormal operand during translation-time evaluation")
    message(FATAL_ERROR "trapping: a denormal operand was not diagnosed\n${stderr}")
endif()
