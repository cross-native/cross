# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The relational operators are signaling comparisons and equality and truth
# tests quiet ones. Where the environment traps invalid, x86-64 $::fmin and
# $::fmax use quiet comparisons instead of MINSS/MAXSS; where it traps invalid
# or denormal operands or flushes, MIPS negation flips the sign bit in general
# registers instead of executing NEG.fmt. An operation that may raise a
# trapped exception is kept even when its result is unused, also by fast-math
# identities.

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed to compile\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
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

# label function pattern: the function must (or with "!" must not) match.
function(expect label function pattern)
    function_text(text ${function})
    if(pattern MATCHES "^!(.*)")
        if(text MATCHES "${CMAKE_MATCH_1}")
            message(FATAL_ERROR "${label}: ${function} matches '${CMAKE_MATCH_1}'\n${text}")
        endif()
    elseif(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} does not match '${pattern}'\n${text}")
    endif()
endfunction()

set(trapping "--model=${MODEL}" -mprofile=trapping-fp)
set(x86 -target x86_64-unknown-linux-gnu -DX87)

compile(x86-default ${x86})
expect(x86-default less "\tcomiss\t")
expect(x86-default equal "\tucomiss\t")
expect(x86-default equal "!\tcomiss\t")
expect(x86-default zero "\tucomisd\t")
expect(x86-default less80 "\tfcomip\t")
expect(x86-default minimum "\tminss\t")
expect(x86-default maximum "\tmaxsd\t")
expect(x86-default discarded "!\t(sqrtsd|mulsd|comisd|cvtsd2ss)\t")

foreach(isa default avx)
    if(isa STREQUAL avx)
        compile(x86-trapping-${isa} ${x86} ${trapping} -mavx)
        set(v v)
    else()
        compile(x86-trapping-${isa} ${x86} ${trapping})
        set(v "")
    endif()
    expect(x86-trapping-${isa} less "\t${v}comiss\t")
    expect(x86-trapping-${isa} minimum "!\t${v}minss\t")
    expect(x86-trapping-${isa} minimum "\t${v}ucomiss\t")
    expect(x86-trapping-${isa} maximum "!\t${v}maxsd\t")
    expect(x86-trapping-${isa} maximum "\t${v}ucomisd\t")
    foreach(operation sqrtsd mulsd comisd cvtsd2ss)
        expect(x86-trapping-${isa} discarded "\t${v}${operation}\t")
    endforeach()
endforeach()

# A fast-math identity deletes an operation only where nothing it may raise
# traps.
compile(x86-fast ${x86} -ffast-math)
expect(x86-fast scaled "!\tmulsd\t")
compile(x86-fast-trapping ${x86} ${trapping} -ffast-math)
expect(x86-fast-trapping scaled "\tmulsd\t")

set(vr4300 -target mips-unknown-elf -march=vr4300 -mabi=o32)
compile(mips-default ${vr4300})
expect(mips-default negated "\tneg[.]s\t")
expect(mips-default negated64 "\tneg[.]d\t")
expect(mips-default discarded "!\t(sqrt|mul|c[.]lt|cvt[.]s)[.]d\t")

compile(mips-trapping ${vr4300} ${trapping})
expect(mips-trapping negated "!\tneg[.]s\t")
expect(mips-trapping negated "\txor\t")
expect(mips-trapping negated64 "!\tneg[.]d\t")
expect(mips-trapping negated64 "\tdmfc1\t")
expect(mips-trapping less "\tc[.]lt[.]s\t")
expect(mips-trapping equal "\tc[.]eq[.]s\t")
foreach(operation "sqrt[.]d" "mul[.]d" "c[.]lt[.]d" "cvt[.]s[.]d")
    expect(mips-trapping discarded "\t${operation}\t")
endforeach()

# MIPS II reaches the sign word of a double through the carrier cell.
compile(r6000-trapping -target mips-unknown-elf -march=r6000 -mabi=o32 ${trapping})
expect(r6000-trapping negated64 "!\tneg[.]d\t")
expect(r6000-trapping negated64 "\tsdc1\t")
