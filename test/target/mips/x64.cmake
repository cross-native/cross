# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The MIPS Cross ABIs x32 and x64: default selection and the shared `x`
# name, x64 at the address width of each triple, its aggregate results,
# object metadata, the n64 bridge, private calls under n64, and the
# address-model and removed-name diagnostics.

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

# The registry defaults to x64 on both mips64 byte orders and to x32 on
# 32-bit triples, and lists `x` with the entry it names for the triple.
foreach(triple mips64-unknown-elf mips64el-unknown-elf mips-unknown-elf)
    execute_process(
        COMMAND "${CC}" --print-abis -target ${triple}
        RESULT_VARIABLE status OUTPUT_VARIABLE listing ERROR_VARIABLE stderr)
    if(triple MATCHES "^mips64")
        set(expected "x64")
        set(other "x32")
    else()
        set(expected "x32")
        set(other "x64")
    endif()
    if(NOT status EQUAL 0 OR NOT listing MATCHES "default: ${expected}\n" OR
       NOT listing MATCHES "\n  ${expected}  aliases: x  " OR
       NOT listing MATCHES "\n  ${other}  aliases:  ")
        message(FATAL_ERROR
            "${triple} ABI registry does not default to ${expected}\n"
            "${listing}\n${stderr}")
    endif()
endforeach()

# The profile default, -mabi=x, and -mabi=x64 are one contract on mips64;
# n64 places floating arguments differently.
foreach(profile mips64-n64 mips64el-n64)
    set(base "${OUTPUT}/${profile}")
    run_cc(${profile}-default -S -O2 -mprofile=${profile} "${CALLEE}"
           -o "${base}-default.s")
    run_cc(${profile}-x -S -O2 -mprofile=${profile} -mabi=x
           "${CALLEE}" -o "${base}-x.s")
    run_cc(${profile}-canonical -S -O2 -mprofile=${profile} -mabi=x64
           "${CALLEE}" -o "${base}-canonical.s")
    run_cc(${profile}-n64 -S -O2 -mprofile=${profile} -mabi=n64
           "${CALLEE}" -o "${base}-n64.s")
    file(READ "${base}-default.s" default_text)
    file(READ "${base}-x.s" x_text)
    file(READ "${base}-canonical.s" canonical_text)
    file(READ "${base}-n64.s" n64_text)
    if(NOT default_text STREQUAL x_text OR
       NOT default_text STREQUAL canonical_text)
        message(FATAL_ERROR "${profile}: the default, x, and x64 ABIs differ")
    endif()
    if(default_text STREQUAL n64_text)
        message(FATAL_ERROR "${profile}: the default ABI is still n64")
    endif()
    # Independent cursors: the f32 is the second floating argument, f14;
    # n64's shared cursor would use f14 for the third argument of any class.
    function_body("${default_text}" x64_mixed mixed)
    if(NOT mixed MATCHES "[$]f12" OR NOT mixed MATCHES "[$]f14" OR
       mixed MATCHES "[$]f13")
        message(FATAL_ERROR
            "${profile}: x64_mixed does not read f12/f14\n${mixed}")
    endif()
    function_body("${n64_text}" x64_mixed n64_mixed)
    if(NOT n64_mixed MATCHES "[$]f13")
        message(FATAL_ERROR
            "${profile}: n64 x64_mixed does not read f13\n${n64_mixed}")
    endif()
    # ELF64 objects are read with the n64 register names.
    if(default_text MATCHES
       "[$](zero|at|v[01]|a[0-3]|t[0-9]|s[0-7]|k[01]|gp|sp|fp|ra)[^a-z0-9_]")
        message(FATAL_ERROR
            "${profile}: Cross assembly names a GPR symbolically\n${default_text}")
    endif()
endforeach()

# On a 32-bit triple `x` is x32, and x64 keeps the transport of the 64-bit
# triple with 32-bit pointers: the same registers, but addresses computed
# with 32-bit instructions.
run_cc(mips-x -S -O2 -target mips-unknown-elf -march=vr4300 -mabi=x
       "${CALLEE}" -o "${OUTPUT}/mips-x.s")
run_cc(mips-x32 -S -O2 -target mips-unknown-elf -march=vr4300 -mabi=x32
       "${CALLEE}" -o "${OUTPUT}/mips-x32.s")
file(READ "${OUTPUT}/mips-x.s" mips_x)
file(READ "${OUTPUT}/mips-x32.s" mips_x32)
if(NOT mips_x STREQUAL mips_x32)
    message(FATAL_ERROR "-mabi=x on a mips triple is not x32")
endif()
run_cc(vr4300-x64-profile -S -O2 -mprofile=vr4300-x64 "${CALLEE}"
       -o "${OUTPUT}/vr4300-x64.s")
run_cc(mips-x64 -S -O2 -target mips-unknown-elf -march=vr4300 -mabi=x64
       -mtune=vr4300 "${CALLEE}" -o "${OUTPUT}/mips-x64.s")
file(READ "${OUTPUT}/vr4300-x64.s" profile_x64)
file(READ "${OUTPUT}/mips-x64.s" mips_x64)
if(NOT profile_x64 STREQUAL mips_x64)
    message(FATAL_ERROR "the vr4300-x64 profile does not select x64")
endif()
function_body("${mips_x64}" x64_mixed narrow_mixed)
if(NOT narrow_mixed MATCHES "[$]f12" OR NOT narrow_mixed MATCHES "[$]f14")
    message(FATAL_ERROR "x64 with 32-bit addresses moved f12/f14\n${narrow_mixed}")
endif()

set(widths_source "${OUTPUT}/widths.x")
file(WRITE "${widths_source}" "struct quad { u64 a; u64 b; u64 c; u64 d; };
struct five { u64 parts[5]; };

global u64 *next_cell(in u64 *cell) {
    return cell + 1;
}

// 256 bits: four registers, v0, v1, a0, and a1.
global struct quad make_quad(in u64 seed) {
    struct quad result;
    result.a = seed + 1u64;
    result.b = seed * 3u64;
    result.c = seed ^ 5u64;
    result.d = seed << 7u64;
    return result;
}

// 320 bits: memory at the hidden address in a0; seed arrives in a1.
global struct five make_five(in u64 seed) {
    struct five result;
    result.parts[0] = seed;
    result.parts[1] = seed * 2u64 + 1u64;
    result.parts[2] = seed * 3u64 + 2u64;
    result.parts[3] = seed * 4u64 + 3u64;
    result.parts[4] = seed * 5u64 + 4u64;
    return result;
}
")
foreach(variant "32|mips-unknown-elf|-march=vr4300|addiu"
                "64|mips64-unknown-elf|-march=mips64|daddiu")
    string(REPLACE "|" ";" variant "${variant}")
    list(GET variant 0 bits)
    list(GET variant 1 triple)
    list(GET variant 2 cpu)
    list(GET variant 3 add)
    run_cc(widths-${bits} -S -O2 -target ${triple} ${cpu} -mabi=x64
           "${widths_source}" -o "${OUTPUT}/widths-${bits}.s")
    file(READ "${OUTPUT}/widths-${bits}.s" widths)
    function_body("${widths}" next_cell next)
    if(NOT next MATCHES "[\t ]${add}[\t ][$](v0|2),[$](a0|4),8\n")
        message(FATAL_ERROR
            "x64 pointers are not ${bits} bits wide on ${triple}\n${next}")
    endif()
    # The last word of a 256-bit result is loaded into a1; a 320-bit
    # result, which x64 once rejected with 32-bit addresses, takes a0 for
    # its address, so the seed arrives in a1.
    function_body("${widths}" make_quad quad)
    if(NOT quad MATCHES "[\t ]ld[\t ][$](a1|5),")
        message(FATAL_ERROR
            "x64 does not return 256 bits in registers on ${triple}\n${quad}")
    endif()
    function_body("${widths}" make_five five)
    if(NOT five MATCHES "[$](a1|5)")
        message(FATAL_ERROR
            "x64 does not return 320 bits through a0 on ${triple}\n${five}")
    endif()
    run_cc(widths-${bits}-object -c -O2 -target ${triple} ${cpu} -mabi=x64
           "${widths_source}" -o "${OUTPUT}/widths-${bits}.o")
    execute_process(
        COMMAND "${LLVM_READOBJ}" --file-headers "${OUTPUT}/widths-${bits}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE header ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR NOT header MATCHES "Class: ${bits}-bit")
        message(FATAL_ERROR
            "x64 on ${triple} is not an ELF${bits} object\n${header}\n${error}")
    endif()
endforeach()

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
# preserves and the Cross ABI clobbers. The caller also uses `x` in a
# function-pointer type.
run_cc(caller -S -O2 -mprofile=mips64-n64 "${CALLER}" -o "${OUTPUT}/caller.s")
file(READ "${OUTPUT}/caller.s" caller_text)
function_body("${caller_text}" x64_entry_weighted entry)
foreach(pattern "[\t ]sd[\t ][$]28," "[\t ]sdc1[\t ][$]f24," "[\t ]sdc1[\t ][$]f31,"
                "[\t ]sd[\t ][$]16,")
    if(NOT entry MATCHES "${pattern}")
        message(FATAL_ERROR
            "n64 entry does not preserve '${pattern}'\n${entry}")
    endif()
endforeach()

# Private calls of a 64-bit-address compilation use x64 even under
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

# The address model of a fixed-width ABI and the triple must agree; x64
# follows the triple, also on one function of a 32-bit compilation.
expect_error(x32-on-mips64
    "'x32' ABI has 32-bit addresses and cannot be selected for a mips64"
    -c -O2 -target mips64-unknown-elf -mabi=x32 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(n64-on-mips
    "'n64' ABI has 64-bit addresses and needs a mips64/mips64el"
    -c -O2 -target mips-unknown-elf -march=vr4300 -mabi=n64 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(x64-without-mips3 "needs the 64-bit MIPS III register file"
    -c -O2 -mprofile=mips64-n64 -march=mips1 "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
expect_error(x64-single-float "defined on a 64-bit FPU"
    -c -O2 -mprofile=mips64-n64 -msingle-float "${CALLEE}"
    -o "${OUTPUT}/mismatch.o")
set(mixed_source "${OUTPUT}/mixed-model.x")
file(WRITE "${mixed_source}" "[[abi(\"x32\")]]
global u32 narrow(in u32 value) {
    return value + 1u32;
}
")
expect_error(mixed-address-models "different address width than the compilation ABI"
    -S -O2 -mprofile=mips64-n64 "${mixed_source}" -o "${OUTPUT}/mixed-model.s")
set(follow_source "${OUTPUT}/follow-model.x")
file(WRITE "${follow_source}" "[[abi(\"x64\")]]
global u64 wide(in u64 value) {
    return value + 1u64;
}
")
run_cc(x64-function-mips -S -O2 -target mips-unknown-elf -march=vr4300
       "${follow_source}" -o "${OUTPUT}/follow-model.s")

# A redeclaration agrees when its ABI names select the same entry: `x` and
# x64 on a mips64 triple, but not on a 32-bit triple, where `x` is x32.
set(redeclare_source "${OUTPUT}/redeclare.x")
file(WRITE "${redeclare_source}" "[[abi(\"x\")]] global u64 twice(in u64 value);
[[abi(\"x64\")]] global u64 twice(in u64 value) {
    return value * 2u64;
}
")
run_cc(redeclare-mips64 -S -O2 -mprofile=mips64-n64 "${redeclare_source}"
       -o "${OUTPUT}/redeclare.s")
expect_error(redeclare-mips
    "redeclare[.]x:2:[0-9]+: error: incompatible redeclaration of function 'twice'"
    -S -O2 -target mips-unknown-elf -march=vr4300 "${redeclare_source}"
    -o "${OUTPUT}/redeclare.s")

# The cross* spellings are not ABI names.
foreach(name cross cross32 cross64 cross-n64 cross_abi)
    expect_error(removed-${name}
        "unknown ABI model '${name}' for target 'mips-unknown-elf'"
        -S -O2 -target mips-unknown-elf -march=vr4300 -mabi=${name}
        "${CALLEE}" -o "${OUTPUT}/removed.s")
endforeach()
set(removed_source "${OUTPUT}/removed.x")
file(WRITE "${removed_source}" "[[abi(\"cross64\")]]
global u64 removed(in u64 value) {
    return value;
}
")
expect_error(removed-attribute
    "removed[.]x:[0-9]+:[0-9]+: error: unknown ABI model 'cross64' for target 'mips64-unknown-elf'"
    -S -O2 -mprofile=mips64-n64 "${removed_source}" -o "${OUTPUT}/removed.s")
expect_error(removed-profile "unknown Cross model profile 'vr4300-cross64'"
    -S -O2 -mprofile=vr4300-cross64 "${CALLEE}" -o "${OUTPUT}/removed.s")
