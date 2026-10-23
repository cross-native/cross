# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# What AArch64 code generation does not support yet fails with one
# source-located diagnostic and writes no output; preprocessing and
# dependency output work for every source.

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(prefix "AArch64 code generation does not yet support")

# Writes SOURCE to NAME.x and expects one error at LINE:COLUMN whose text
# matches MESSAGE, at -O0 and -O2.
function(unsupported name line column message source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level -O0 -O2)
        file(REMOVE "${OUTPUT}/${name}.s")
        execute_process(COMMAND "${CC}" -target aarch64-none-elf ${level} -S "${OUTPUT}/${name}.x"
                                -o "${OUTPUT}/${name}.s"
                        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        string(REGEX MATCHALL "error:" errors "${err}")
        list(LENGTH errors count)
        if(status EQUAL 0 OR NOT count EQUAL 1 OR
           NOT err MATCHES "${name}\\.x:${line}:${column}: error: ${message}" OR
           EXISTS "${OUTPUT}/${name}.s")
            message(FATAL_ERROR "${name} ${level}: expected one error at ${line}:${column} "
                                "matching '${message}'\n${out}${err}")
        endif()
    endforeach()
endfunction()

unsupported(floating 1 25 "${prefix} floating-point values" "global f64 scale(in f64 x) {
    return x * 2.0f64;
}
")
unsupported(vector 3 15 "${prefix} vector values" "typedef u32 u32x4 [[vector_size(16)]];
global u32 lane(in u32 a) {
    u32x4 v = (u32x4)a;
    return v[1];
}
")
unsupported(variadic 1 8 "${prefix} variadic functions" "global u32 first(in u32 count, ...) {
    return count;
}
")
unsupported(naked 1 18 "${prefix} naked functions" "[[naked]] global void entry() {
}
")
unsupported(manual 1 8 "${prefix} manual register endpoints; use a registered ABI"
"global u32 pinned(in u32 a \"x5\") {
    return a;
}
")
unsupported(vla 2 9 "${prefix} variable-length arrays" "global u32 sum(in u32 n) {
    u32 values[n];
    values[0] = n;
    return values[0];
}
")
unsupported(tls 3 12 "${prefix} thread-local objects" "[[thread_local]] static u32 slot = 1u32;
global u32 read() {
    return slot;
}
")
unsupported(atomic 3 5 "selected target 'aarch64' has no runtime-free atomic operation for u32"
"u32 [[atomic]] counter = 0u32;
global void bump() {
    counter = 1u32;
}
")

unsupported(tail_stack 6 25 "${prefix} \\[\\[musttail\\]\\] calls with stack arguments"
"[[abi(\"aapcs64\"), noinline]] static u32 nine(in u32 a1, in u32 a2, in u32 a3, in u32 a4,
        in u32 a5, in u32 a6, in u32 a7, in u32 a8, in u32 a9) {
    return a1 + a9;
}
[[abi(\"aapcs64\")]] global u32 forward(in u32 x) {
    [[musttail]] return nine(x, x, x, x, x, x, x, x, x);
}
")
unsupported(tail_preserved 9 25
"${prefix} \\[\\[musttail\\]\\] calls that pass arguments in registers the caller preserves"
"[[noinline]] static u32 eighteen(in u32 a1, in u32 a2, in u32 a3, in u32 a4, in u32 a5,
        in u32 a6, in u32 a7, in u32 a8, in u32 a9, in u32 a10, in u32 a11, in u32 a12,
        in u32 a13, in u32 a14, in u32 a15, in u32 a16, in u32 a17, in u32 a18) {
    return a1 + a17 + a18;
}
// x passes the seventeenth and eighteenth arguments in x19 and x20, which
// the caller restores before a tail transfer.
global u32 forward(in u32 x) {
    [[musttail]] return eighteen(x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x);
}
")
unsupported(tail_reference 8 25 "${prefix} \\[\\[musttail\\]\\] calls that pass records by reference"
"struct big {
    u64 words[3];
};
[[abi(\"aapcs64\"), noinline]] static u64 first(in struct big b) {
    return b.words[0];
}
[[abi(\"aapcs64\")]] global u64 forward(in struct big b) {
    [[musttail]] return first(b);
}
")
unsupported(tail_memory 12 25 "${prefix} \\[\\[musttail\\]\\] calls that return records in memory"
"struct big {
    u64 words[3];
};
[[abi(\"aapcs64\"), noinline]] static struct big make(in u64 x) {
    struct big b;
    b.words[0] = x;
    b.words[1] = x + 1u64;
    b.words[2] = x + 2u64;
    return b;
}
[[abi(\"aapcs64\")]] global struct big forward(in u64 x) {
    [[musttail]] return make(x);
}
")

# Preprocessing and dependencies need no code generation.
set(valid "${OUTPUT}/valid.x")
file(WRITE "${valid}" "global i32 add(in i32 a, in i32 b) {\n    return a + b;\n}\n")
execute_process(COMMAND "${CC}" -target aarch64-none-elf -E "${valid}" -o -
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "global i32 add[(]in i32 a, in i32 b[)]")
    message(FATAL_ERROR "-E failed for aarch64\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -target aarch64-none-elf -M "${valid}"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "^valid[.]o: [^\n]*valid[.]x\n$")
    message(FATAL_ERROR "-M failed for aarch64\n${out}\n${err}")
endif()
# A Mach-O or COFF AArch64 triple has no code generator yet.
execute_process(COMMAND "${CC}" -target aarch64-apple-darwin -S "${valid}" -o "${OUTPUT}/darwin.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "error: AArch64 code generation supports ELF objects only")
    message(FATAL_ERROR "aarch64-apple-darwin was not rejected\n${out}\n${err}")
endif()
