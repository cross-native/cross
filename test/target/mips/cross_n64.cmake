# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The Cross ABI of the 64-bit MIPS address model: default selection, the
# shared `cross` name, object metadata, the n64 bridge, private calls under
# n64, and address-model diagnostics.

foreach(required CC CALLEE CALLER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
find_program(LLVM_READELF NAMES llvm-readelf REQUIRED)
file(MAKE_DIRECTORY "${OUTPUT}")

function(run_cc label)
    execute_process(
        COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(expect_error label expected)
    execute_process(
        COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

# Extracts one function's assembly between its .ent and .end directives.
function(function_body text name variable)
    string(FIND "${text}" ".ent ${name}\n" begin)
    string(FIND "${text}" ".end ${name}\n" end)
    if(begin EQUAL -1 OR end LESS begin)
        message(FATAL_ERROR "no assembly for '${name}'\n${text}")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

# The registry names cross-n64 as the default of both mips64 byte orders and
# keeps cross32 for 32-bit triples.
foreach(triple mips64-unknown-elf mips64el-unknown-elf mips-unknown-elf)
    execute_process(
        COMMAND "${CC}" --print-abis -target ${triple}
        RESULT_VARIABLE status OUTPUT_VARIABLE listing ERROR_VARIABLE stderr)
    if(triple MATCHES "^mips64")
        set(expected "cross-n64")
    else()
        set(expected "cross32")
    endif()
    if(NOT status EQUAL 0 OR NOT listing MATCHES "default: ${expected}\n" OR
       NOT listing MATCHES "  cross-n64  aliases: cross cross_abi ")
        message(FATAL_ERROR
            "${triple} ABI registry does not default to ${expected}\n"
            "${listing}\n${stderr}")
    endif()
endforeach()

# The profile default, -mabi=cross, and -mabi=cross-n64 are one contract on
# mips64; n64 places floating arguments differently.
foreach(profile mips64-n64 mips64el-n64)
    set(base "${OUTPUT}/${profile}")
    run_cc(${profile}-default -S -O2 -mprofile=${profile} "${CALLEE}"
           -o "${base}-default.s")
    run_cc(${profile}-cross -S -O2 -mprofile=${profile} -mabi=cross
           "${CALLEE}" -o "${base}-cross.s")
    run_cc(${profile}-canonical -S -O2 -mprofile=${profile} -mabi=cross-n64
           "${CALLEE}" -o "${base}-canonical.s")
    run_cc(${profile}-n64 -S -O2 -mprofile=${profile} -mabi=n64
           "${CALLEE}" -o "${base}-n64.s")
    file(READ "${base}-default.s" default_text)
    file(READ "${base}-cross.s" cross_text)
    file(READ "${base}-canonical.s" canonical_text)
    file(READ "${base}-n64.s" n64_text)
    if(NOT default_text STREQUAL cross_text OR
       NOT default_text STREQUAL canonical_text)
        message(FATAL_ERROR
            "${profile}: the default, cross, and cross-n64 ABIs differ")
    endif()
    if(default_text STREQUAL n64_text)
        message(FATAL_ERROR "${profile}: the default ABI is still n64")
    endif()
    # Independent cursors: the f32 is the second floating argument, f14;
    # n64's shared cursor would use f14 for the third argument of any class.
    function_body("${default_text}" cross_n64_mixed mixed)
    if(NOT mixed MATCHES "[$]f12" OR NOT mixed MATCHES "[$]f14" OR
       mixed MATCHES "[$]f13")
        message(FATAL_ERROR
            "${profile}: cross_n64_mixed does not read f12/f14\n${mixed}")
    endif()
    function_body("${n64_text}" cross_n64_mixed n64_mixed)
    if(NOT n64_mixed MATCHES "[$]f13")
        message(FATAL_ERROR
            "${profile}: n64 cross_n64_mixed does not read f13\n${n64_mixed}")
    endif()
    # ELF64 objects are read with the n64 register names.
    if(default_text MATCHES
       "[$](zero|at|v[01]|a[0-3]|t[0-9]|s[0-7]|k[01]|gp|sp|fp|ra)[^a-z0-9_]")
        message(FATAL_ERROR
            "${profile}: Cross assembly names a GPR symbolically\n${default_text}")
    endif()
endforeach()

# On a 32-bit triple `cross` remains cross32.
run_cc(mips-cross -S -O2 -target mips-unknown-elf -march=vr4300 -mabi=cross
       "${CALLEE}" -o "${OUTPUT}/mips-cross.s")
run_cc(mips-cross32 -S -O2 -target mips-unknown-elf -march=vr4300
       -mabi=cross32 "${CALLEE}" -o "${OUTPUT}/mips-cross32.s")
file(READ "${OUTPUT}/mips-cross.s" mips_cross)
file(READ "${OUTPUT}/mips-cross32.s" mips_cross32)
if(NOT mips_cross STREQUAL mips_cross32)
    message(FATAL_ERROR "-mabi=cross on a mips triple is not cross32")
endif()

# Cross objects of the 64-bit address model are ELF64 n64-class objects on
# the 64-bit FPU, even for a CPU whose features default to FP32.
foreach(variant "mips64-n64:" "mips64el-n64:" "mips64-n64:vr4300")
    string(REPLACE ":" ";" variant "${variant}")
    list(GET variant 0 profile)
    list(GET variant 1 cpu)
    set(extra "")
    if(cpu)
        set(extra -march=${cpu})
    endif()
    set(object "${OUTPUT}/${profile}${cpu}.o")
    run_cc(${profile}-object -c -O2 -mprofile=${profile} ${extra} "${CALLEE}"
           -o "${object}")
    execute_process(
        COMMAND "${LLVM_READOBJ}" --file-headers "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE header ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR NOT header MATCHES "Class: 64-bit" OR
       NOT header MATCHES "EF_MIPS_ARCH_")
        message(FATAL_ERROR "'${object}' is not an ELF64 MIPS object\n${header}\n${error}")
    endif()
    foreach(forbidden "EF_MIPS_ABI2" "EF_MIPS_ABI_O32" "EF_MIPS_ABI_EABI")
        if(header MATCHES "${forbidden}")
            message(FATAL_ERROR "'${object}' is tagged '${forbidden}'\n${header}")
        endif()
    endforeach()
    execute_process(
        COMMAND "${LLVM_READELF}" -A "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE flags ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR NOT flags MATCHES "GPR size: 64" OR
       NOT flags MATCHES "CPR1 size: 64" OR
       NOT flags MATCHES "FP ABI: Hard float")
        message(FATAL_ERROR "'${object}' has incorrect ABI flags\n${flags}\n${error}")
    endif()
endforeach()

# An n64 entry calling Cross functions saves the gp and f24-f31 that n64
# preserves and the Cross ABI clobbers. The caller also uses `cross` in a
# function-pointer type.
run_cc(caller -S -O2 -mprofile=mips64-n64 "${CALLER}" -o "${OUTPUT}/caller.s")
file(READ "${OUTPUT}/caller.s" caller_text)
function_body("${caller_text}" cross_n64_entry_weighted entry)
foreach(pattern "[\t ]sd[\t ][$]28," "[\t ]sdc1[\t ][$]f24," "[\t ]sdc1[\t ][$]f31,"
                "[\t ]sd[\t ][$]16,")
    if(NOT entry MATCHES "${pattern}")
        message(FATAL_ERROR
            "n64 entry does not preserve '${pattern}'\n${entry}")
    endif()
endforeach()

# Private calls of a 64-bit-address compilation use cross-n64 even under
# -mabi=n64: the helper reads its double from f12 rather than n64's f13.
set(private_source "${OUTPUT}/private.x")
file(WRITE "${private_source}" "[[noinline]]
static u64 private_pick(in u64 count, in f64 value) {
    if (value > 1.0f64) return count + 1u64;
    return count;
}

[[abi(\"n64\")]]
global u64 private_entry(in u64 count, in f64 value) {
    return private_pick(count, value) * 3u64;
}
")
foreach(mode private-abi no-private-abi)
    run_cc(${mode} -S -O2 -mprofile=mips64-n64 -mabi=n64 -f${mode}
           "${private_source}" -o "${OUTPUT}/${mode}.s")
    file(READ "${OUTPUT}/${mode}.s" private_text)
    string(REGEX MATCH "[_a-zA-Z0-9]*private_pick" helper "${private_text}")
    function_body("${private_text}" "${helper}" helper_body)
    if(mode STREQUAL "private-abi")
        set(wanted "[$]f12")
        set(unwanted "[$]f13")
    else()
        set(wanted "[$]f13")
        set(unwanted "[$]f12")
    endif()
    if(NOT helper_body MATCHES "${wanted}" OR helper_body MATCHES "${unwanted}")
        message(FATAL_ERROR
            "-f${mode} helper does not read '${wanted}'\n${helper_body}")
    endif()
endforeach()

# The address model of the ABI and the triple must agree.
expect_error(cross32-on-mips64
    "'cross32' ABI has 32-bit addresses and cannot be selected for a mips64"
    -c -O2 -target mips64-unknown-elf -mabi=cross32 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(cross64-on-mips64
    "'cross64' ABI has 32-bit addresses and cannot be selected for a mips64"
    -c -O2 -target mips64-unknown-elf -mabi=cross64 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(cross-n64-on-mips
    "'cross-n64' ABI has 64-bit addresses and needs a mips64/mips64el"
    -c -O2 -target mips-unknown-elf -mabi=cross-n64 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(cross-n64-without-mips3 "needs the 64-bit MIPS III register file"
    -c -O2 -mprofile=mips64-n64 -march=mips1 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(cross-n64-single-float "defined on a 64-bit FPU"
    -c -O2 -mprofile=mips64-n64 -msingle-float "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
set(mixed_source "${OUTPUT}/mixed-model.x")
file(WRITE "${mixed_source}" "[[abi(\"cross32\")]]
global u32 narrow(in u32 value) {
    return value + 1u32;
}
")
expect_error(mixed-address-models "different address width than the compilation ABI"
    -S -O2 -mprofile=mips64-n64 "${mixed_source}" -o "${OUTPUT}/mixed-model.s")
