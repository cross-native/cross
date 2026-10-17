# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# With AVX, a splat of a loaded scalar broadcasts from memory, a stored
# floating lane stores from its vector, and the rows of the affine
# transform pack into one vector computation; without memory-operand
# folding the loads and stores stay separate.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile suffix)
    execute_process(
        COMMAND "${CC}" -S -O2 -march=x86-64-v3 -target x86_64-unknown-linux-gnu
                ${ARGN} "${SOURCE}" -o "${OUTPUT}${suffix}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "lane-memory compilation failed\n${stdout}\n${stderr}")
    endif()
endfunction()
compile("")
compile(".unfolded" -fno-fold-memory-operands)

function(body file function variable)
    file(READ "${file}" assembly)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    set(${variable} "${text}" PARENT_SCOPE)
endfunction()

function(has function pattern)
    body("${OUTPUT}.s" ${function} text)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${function} lacks '${pattern}'\n${text}")
    endif()
endfunction()

has(transform "\tvbroadcastss\t[0-9]*[(]%")
has(transform "\tvmulps\t")
has(transform "[.]vector[.][0-9]+[(]%rip[)]")
has(transform "\tvmovss\t%xmm[0-9]+, [0-9]*[(]%")
has(transform "\tvextractps\t[$]2, %xmm[0-9]+, [0-9]*[(]%")
has(scaled "\tvmovddup\t[0-9]*[(]%")
has(widened "\tvbroadcastsd\t[0-9]*[(]%")
has(singles "\tvbroadcastss\t[0-9]*[(]%")
has(split "\tvmovhps\t%xmm[0-9]+, [0-9]*[(]%")

# Without AVX, 256-bit values live in XMM halves: no AVX instruction, such
# as VZEROUPPER, may appear.
execute_process(
    COMMAND "${CC}" -S -O2 -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.sse2.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "baseline lane-memory compilation failed\n${stderr}")
endif()
file(READ "${OUTPUT}.sse2.s" baseline)
if(baseline MATCHES "\t(vzeroupper|v[a-z0-9]+)\t?[^\n]*\n")
    string(REGEX MATCH "\t(vzeroupper|v[a-z0-9]+)[^\n]*" offending "${baseline}")
    message(FATAL_ERROR "baseline x86-64 output uses AVX: ${offending}")
endif()

body("${OUTPUT}.unfolded.s" split unfolded)
if(unfolded MATCHES "\tvmovhps\t%xmm[0-9]+, [0-9]*[(]%")
    message(FATAL_ERROR
        "-fno-fold-memory-operands stored a lane from its vector\n${unfolded}")
endif()
