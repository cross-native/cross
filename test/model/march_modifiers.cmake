# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -march=CPU+FEATURE+noFEATURE on every target: the CPU is the longest
# prefix that names a cpu entry, and the modifiers change its features in
# order; explicit -m options override them.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# The value of each `m.NAME` in `expected` ("NAME=on" or "NAME=off") after
# cc with ARGN.
function(expect_options label expected)
    execute_process(COMMAND "${CC}" ${ARGN} --print-options=target
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${out}\n${err}")
    endif()
    foreach(item IN LISTS expected)
        string(REPLACE "=" ";" pair "${item}")
        list(GET pair 0 name)
        list(GET pair 1 value)
        string(REPLACE "." "[.]" pattern "${name}")
        if(NOT out MATCHES "(^|\n)m[.]${pattern}  type: boolean  value: ${value}  ")
            message(FATAL_ERROR "${label}: m.${name} is not ${value}\n${out}")
        endif()
    endforeach()
endfunction()

expect_options(x86-disable "avx=on;fma=on;avx2=off"
    -target x86_64-unknown-linux-gnu -march=haswell+noavx2)
expect_options(x86-enable "sse4.2=on;popcnt=on"
    -target x86_64-unknown-linux-gnu -march=x86-64+sse4.2+popcnt)
expect_options(x86-explicit "avx2=on"
    -target x86_64-unknown-linux-gnu -march=haswell+noavx2 -mavx2)
expect_options(mips-modifier "fix4300=on;mips3=on"
    -target mips-unknown-elf -march=vr4300+fix4300)

# A cpu name with a '+' stays whole.
execute_process(COMMAND "${CC}" -target mips64-unknown-elf -march=octeon+ --print-costs
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "^cpu: octeon[+]  tune: octeon[+]  ")
    message(FATAL_ERROR "-march=octeon+ did not select octeon+\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -march=haswell+avx3
        --print-options=target
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "error: unknown x86-64 feature 'avx3' in 'haswell[+]avx3'")
    message(FATAL_ERROR "an unknown x86-64 modifier was not reported\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -target mips-unknown-elf -march=r3000+allegrex
        --print-options=target
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "error: mips feature 'allegrex' in 'r3000[+]allegrex' is a CPU fact that a modifier cannot change")
    message(FATAL_ERROR "a CPU-fact modifier was not reported\n${out}\n${err}")
endif()
