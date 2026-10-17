# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile level)
    execute_process(
        COMMAND "${CC}" -S -${level} -fno-eval-calls
                -target x86_64-unknown-linux-gnu -march=x86-64-v3 "${SOURCE}"
                -o "${OUTPUT}-${level}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "conditional reduction compilation failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${level}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

# At O2 the budgets keep the branches of the contacts loop, but converted
# for its eight lanes the loop vectorizes, so it is converted. The loop
# that calls a function does not vectorize and keeps its four branches.
compile(O2)

function(function_body symbol variable)
    string(FIND "${assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

function_body(contacts contacts)
if(NOT contacts MATCHES "vcmpps[\t ]+[$][0-9]+, %ymm" OR contacts MATCHES "comiss")
    message(FATAL_ERROR "contacts was not vectorized at O2\n${contacts}")
endif()
function_body(called_contacts called)
string(REGEX MATCHALL "\tj[a-ln-z][a-z]*\t" jumps "${called}")
list(LENGTH jumps jump_count)
if(called MATCHES "%ymm" OR jump_count LESS 4)
    message(FATAL_ERROR "called_contacts lost its branches at O2\n${called}")
endif()

compile(O3)

# The counts are selected per lane: floating comparisons of 32-bit lanes,
# and the 64-bit index compared in 32-bit lanes with the clamped invariant.
function_body(contacts contacts)
if(NOT contacts MATCHES "vcmpps[\t ]+[$][0-9]+, %ymm" OR
   NOT contacts MATCHES "vpcmpeqd[\t ]+%ymm" OR
   contacts MATCHES "comiss")
    message(FATAL_ERROR "contacts was not vectorized in 32-bit lanes\n${contacts}")
endif()
function_body(weighted_below weighted_below)
if(NOT weighted_below MATCHES "vpcmpgtd[\t ]+%ymm" OR
   NOT weighted_below MATCHES "vpaddd")
    message(FATAL_ERROR "weighted_below was not vectorized\n${weighted_below}")
endif()
# A function that uses 256-bit registers spells its SSE instructions with
# VEX, so its scalar update does not merge with dirty upper halves.
function_body(drifting_contacts drifting)
if(NOT drifting MATCHES "%ymm" OR
   drifting MATCHES "\t(movss|movsd|movd|movq|movaps|xorps|addss)\t[^\n]*%xmm")
    message(FATAL_ERROR "drifting_contacts mixes SSE and AVX encodings\n${drifting}")
endif()
function_body(contacts_reference reference)
if(reference MATCHES "%ymm")
    message(FATAL_ERROR "the counting-down reference was vectorized\n${reference}")
endif()
