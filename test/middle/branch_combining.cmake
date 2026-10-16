# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under -fif-conversion, && and || chains whose work may run unconditionally
# become one condition, and their loads run ahead of the tests only where
# their indices are in bounds; a load its guard keeps in bounds stays behind
# the guard.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed to compile\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" assembly)
    string(REPLACE ";" "," assembly "${assembly}")
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

function(branches label function expected)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    string(REGEX MATCHALL "\tj[a-ln-z][a-z]*\t" found "${text}")
    list(LENGTH found count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${label}: ${function} has ${count} conditional jumps, not ${expected}\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu -fno-eval-calls)
foreach(march x86-64 x86-64-v3)
    compile(${march} -O3 ${x86} -march=${march})
    branches(${march} count_near 1)
    branches(${march} any_match 0)
    branches(${march} masked 0)
    branches(${march} either 0)
    branches(${march} guarded 2)
endforeach()
compile(no-if-conversion -O3 ${x86} -fno-if-conversion)
branches(no-if-conversion any_match 3)
branches(no-if-conversion either 2)
