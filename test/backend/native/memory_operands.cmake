# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Floating reads through a pointer at a constant displacement fold into
# x86-64 SSE/AVX memory operands under -ffold-memory-operands.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${ARGN}
                            "${SOURCE}" -o "${OUTPUT}/${label}.s"
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
    string(FIND "${assembly}" "\n.size ${function}," end)
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} has '${pattern}'\n${text}")
    endif()
endfunction()

compile(folded -O2)
expect(folded affine "\tmulss\t8[(]%[a-z0-9]+[)], " TRUE)
expect(folded affine "\taddss\t12[(]%[a-z0-9]+[)], " TRUE)
expect(folded ordered "\tdivss\t8[(]%[a-z0-9]+[)], " TRUE)
compile(avx -O2 -march=x86-64-v3)
expect(avx affine "\tvmulss\t8[(]%[a-z0-9]+[)], " TRUE)
compile(unfolded -O2 -fno-fold-memory-operands)
expect(unfolded affine "\tmovss\t8[(]%[a-z0-9]+[)], " TRUE)
expect(unfolded affine "\tmulss\t[0-9]*[(]" FALSE)
