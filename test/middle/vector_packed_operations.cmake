# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The reduction vectorizer asks the target for its packed operations: a loop
# whose operation the CPU would compute one lane at a time stays scalar, and
# vectorizes where the CPU has the packed instruction.

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

function(expect label function pattern present)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} has '${pattern}'\n${text}")
    endif()
endfunction()

set(x86 -O2 -target x86_64-unknown-linux-gnu)
set(packed "\tv?padd[dq]\t")
compile(sse2 ${x86} -march=x86-64)
expect(sse2 weighted "\tpmuludq\t" TRUE)
expect(sse2 weighted "\tpmulld\t" FALSE)
expect(sse2 weighted64 "${packed}" FALSE)
expect(sse2 shifted "${packed}|\tv?pxor\t" FALSE)
compile(sse41 ${x86} -march=x86-64-v2)
expect(sse41 weighted "\tpmulld\t" TRUE)
expect(sse41 weighted64 "${packed}" FALSE)
compile(avx2 ${x86} -march=x86-64-v3)
expect(avx2 weighted "\tvpmulld\t" TRUE)
expect(avx2 weighted64 "\tvpmuludq\t" TRUE)
expect(avx2 shifted "\tvpsllvd\t" TRUE)
