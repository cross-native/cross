# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE CODEGEN_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(expect_error case needle)
    execute_process(
        COMMAND "${CC}" -S -O2 -mprofile=vr4300-o32 -mllsc -DCASE=${case}
                "${SOURCE}" -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "case ${case} unexpectedly compiled")
    endif()
    string(FIND "${stderr}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "case ${case} did not report '${needle}'\n${stdout}\n${stderr}")
    endif()
endfunction()

expect_error(0 "has no runtime-free atomic operation for u128")
expect_error(1 "naked parameter 'value' must name one target register")
expect_error(2 "no contiguous $::patch materializer for u128")

function(assembly label)
    execute_process(
        COMMAND "${CC}" -S -O2 ${ARGN} "${CODEGEN_SOURCE}"
                -o "${OUTPUT}-${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} compilation failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${label}.s" text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

# The body of one function of the last assembly.
function(body name)
    string(REGEX MATCH "\n${name}:\n.*\n[.]end[\t ]+${name}\n" match "${text}")
    set(code "${match}" PARENT_SCOPE)
endfunction()

function(expect pattern what)
    if(NOT code MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: missing '${pattern}'\n${code}")
    endif()
endfunction()

function(reject pattern what)
    if(code MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: unexpected '${pattern}'\n${code}")
    endif()
endfunction()

# n64: halves in registers, a carry from sltu, and the multiply-high of
# zero-extended operands as one dmultu.
assembly(n64 -mprofile=mips64-n64 -mabi=n64)
body(add)
expect("[\t ]daddu[\t ]" "n64 add")
expect("[\t ]sltu[\t ]" "n64 add")
reject("[$]29[)]" "n64 add")
body(product_high)
expect("[\t ]dmultu[\t ][$]4,[$]5" "n64 multiply-high")
expect("[\t ]mfhi[\t ][$]2" "n64 multiply-high")
reject("[\t ]dmult[\t ]|[\t ]jal[\t ]" "n64 multiply-high")
# The argument after a word takes a2 and a3, the low half in a3 on a
# big-endian target and in a2 on a little-endian one.
body(low_half)
expect("[$]7" "n64 big-endian 128-bit argument")
reject("[$]5[^0-9]" "n64 big-endian 128-bit argument")
assembly(n64el -mprofile=mips64el-n64 -mabi=n64)
body(low_half)
expect("[$]6" "n64 little-endian 128-bit argument")
reject("[$]7|[$]5[^0-9]" "n64 little-endian 128-bit argument")

# o32, eabi32, and x32 return the words in v0, v1, a0, and a1, the most
# significant first on a big-endian target.
foreach(abi o32 eabi32 x32)
    assembly(${abi} -target mips-unknown-elf -march=vr4300 -mabi=${abi})
    body(words)
    expect("li[\t ][$](v0|2),1\n" "${abi} 128-bit result word 0")
    expect("li[\t ][$](v1|3),2\n" "${abi} 128-bit result word 1")
    expect("li[\t ][$](a0|4),3\n" "${abi} 128-bit result word 2")
    expect("li[\t ][$](a1|5),4\n" "${abi} 128-bit result word 3")
    assembly(${abi}el -target mipsel-unknown-elf -march=vr4300 -mabi=${abi})
    body(words)
    expect("li[\t ][$](v0|2),4\n" "${abi} little-endian 128-bit result word 0")
    expect("li[\t ][$](a1|5),1\n" "${abi} little-endian 128-bit result word 3")
endforeach()
