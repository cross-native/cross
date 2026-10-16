# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under -fmove-loop-invariants, stores to fixed locations leave their loops
# for the loop exits on x86-64 and MIPS; -fno-move-loop-invariants keeps them
# inside, and so does a read of the location, a call, a store through a
# pointer that may alias, a volatile access, or a floating operation that may
# raise an enabled trap in the loop.

foreach(required CC SOURCE MODEL OUTPUT)
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
    file(READ "${OUTPUT}/${label}.s" text)
    string(REPLACE ";" "," text "${text}")
    set(assembly "${text}" PARENT_SCOPE)
    set(label "${label}" PARENT_SCOPE)
endfunction()

# Sets `body` to the text of function `name` and `loops` to the text of its
# loops: from the label of each backward branch through the branch and the
# instruction after it, which is a delay slot on MIPS.
function(find_loops name)
    string(FIND "${assembly}" "\n${name}:\n" start)
    string(FIND "${assembly}" "\n.size ${name}," end)
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${name}\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    string(REGEX MATCHALL "${branch}[^\n]*\n[^\n]*\n" jumps "${text}")
    set(found "")
    foreach(jump IN LISTS jumps)
        string(REGEX MATCH "[.]L[A-Za-z0-9_.]+" target "${jump}")
        string(FIND "${text}" "\n${target}:\n" from)
        string(FIND "${text}" "${jump}" to)
        if(NOT target STREQUAL "" AND from GREATER -1 AND from LESS to)
            string(LENGTH "${jump}" jump_length)
            math(EXPR length "${to} + ${jump_length} - ${from}")
            string(SUBSTRING "${text}" ${from} ${length} loop)
            string(APPEND found "${loop}")
        endif()
    endforeach()
    if(found STREQUAL "")
        message(FATAL_ERROR "${label}: ${name} has no loop\n${text}")
    endif()
    set(body "${text}" PARENT_SCOPE)
    set(loops "${found}" PARENT_SCOPE)
endfunction()

# The store to `symbol` in `name` is outside its loops but still emitted.
function(expect_sunk name symbol)
    find_loops(${name})
    string(REPLACE "@" "${symbol}" pattern "${store}")
    if(loops MATCHES "${pattern}" OR NOT body MATCHES "${pattern}")
        message(FATAL_ERROR
            "${label}: the store to ${symbol} did not leave the loop of ${name}\n${body}")
    endif()
endfunction()

function(expect_kept name symbol)
    find_loops(${name})
    string(REPLACE "@" "${symbol}" pattern "${store}")
    if(NOT loops MATCHES "${pattern}")
        message(FATAL_ERROR
            "${label}: the store to ${symbol} left the loop of ${name}\n${body}")
    endif()
endfunction()

foreach(target x86_64 mips)
    if(target STREQUAL "x86_64")
        set(flags -target x86_64-unknown-linux-gnu)
        set(branch "\tj[a-z]+\t")
        set(store "\tmov[a-z]*\t[^\n]*, @[(]%rip[)]")
    else()
        set(flags -target mips-unknown-elf -march=vr4300 -mabi=o32)
        set(branch "\tb[a-z]*\t")
        set(store "\ts[bhwd](c1)?\t[^\n]*@")
    endif()
    foreach(mode O2 O3)
        compile(${target}-${mode} -${mode} ${flags})
        expect_sunk(sum_stored total)
        expect_sunk(wide_stored wide_total)
        expect_sunk(wide_stored mean)
        expect_sunk(countdown remaining)
        expect_kept(load_kept accumulated)
        expect_kept(call_kept counted)
        expect_kept(alias_kept aliased)
        expect_kept(volatile_kept near_device)
    endforeach()
    # Without rotation only the bottom-tested loop leaves after its store.
    compile(${target}-Os -Os ${flags})
    expect_sunk(countdown remaining)
    compile(${target}-no-move-loop-invariants -O2 -fno-move-loop-invariants
            ${flags})
    expect_kept(sum_stored total)
    expect_kept(wide_stored wide_total)
    expect_kept(wide_stored mean)
    expect_kept(countdown remaining)
    # A trap must observe the stores of the passes before it.
    compile(${target}-trapping -O2 ${flags} "--model=${MODEL}"
            -mprofile=trapping-fp)
    expect_sunk(sum_stored total)
    expect_kept(wide_stored mean)
endforeach()
