# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Division by a constant selects multiply-high sequences under a speed
# objective, keeps the divide under a size objective or -fno-div-by-constant,
# and honors an explicit -fdiv-by-constant at -O0.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(compile name)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}" -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "division compile ${name} failed\n${err}")
    endif()
    file(READ "${OUTPUT}-${name}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

function(body function terminator)
    string(FIND "${assembly}" "\n${function}:" start)
    if(start LESS 0)
        message(FATAL_ERROR "missing function ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "${terminator} ${function}" end)
    string(SUBSTRING "${text}" 0 ${end} text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(expect mode function terminator pattern present)
    body(${function} "${terminator}")
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${mode}: ${function} lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${mode}: ${function} has '${pattern}'\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(x86_divide "\t(i)?div[lq]\t")

compile(x86-O2 ${x86} -O2)
string(REGEX MATCHALL "\n(quotient|remainder)_[0-9]+:" labels "${assembly}")
list(LENGTH labels count)
if(count LESS 200)
    message(FATAL_ERROR "expected every divisor function, found ${count}")
endif()
foreach(label IN LISTS labels)
    string(REGEX REPLACE "[\n:]" "" function "${label}")
    expect(x86-O2 ${function} ".size" "${x86_divide}" FALSE)
endforeach()
expect(x86-O2 quotient_508 ".size" "\tmull\t" TRUE)
expect(x86-O2 quotient_609 ".size" "\timull\t" TRUE)
expect(x86-O2 quotient_707 ".size" "\tmulq\t" TRUE)
expect(x86-O2 quotient_808 ".size" "\timulq\t" TRUE)

compile(x86-disabled ${x86} -O2 -fno-div-by-constant)
expect(x86-disabled quotient_508 ".size" "${x86_divide}" TRUE)
expect(x86-disabled quotient_502 ".size" "${x86_divide}" TRUE)

compile(x86-Os ${x86} -Os)
expect(x86-Os quotient_508 ".size" "${x86_divide}" TRUE)
expect(x86-Os quotient_502 ".size" "${x86_divide}" FALSE)
expect(x86-Os remainder_518 ".size" "${x86_divide}" FALSE)

compile(x86-O0-enabled ${x86} -O0 -fdiv-by-constant)
expect(x86-O0-enabled quotient_508 ".size" "${x86_divide}" FALSE)

set(r3000 -target mips-unknown-elf -march=r3000 -mabi=o32)
compile(r3000-O2 ${r3000} -O2)
expect(r3000-O2 quotient_508 ".end" "\tdivu?\t" FALSE)
expect(r3000-O2 quotient_508 ".end" "\tmultu\t[^\n]*\n\tmfhi\t" TRUE)
expect(r3000-O2 quotient_609 ".end" "\tmult\t[^\n]*\n\tmfhi\t" TRUE)
expect(r3000-O2 remainder_613 ".end" "\tdivu?\t" FALSE)

set(vr4300 -target mips-unknown-elf -march=vr4300 -mabi=o32)
compile(vr4300-O2 ${vr4300} -O2)
expect(vr4300-O2 quotient_707 ".end" "\tddivu\t" FALSE)
expect(vr4300-O2 quotient_707 ".end" "\tdmultu\t" TRUE)
expect(vr4300-O2 quotient_810 ".end" "\tdmult\t" TRUE)
