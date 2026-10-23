# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Brace lists that start from a value: a base of the list's own type, an
# aggregate member initialized from a value, and later entries that replace
# parts of either. Every other overlap of entries stays an error, in automatic,
# block-static, and file-scope initializers and in compound literals.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    if(NOT ARGN)
        set(ARGN -target x86_64-unknown-linux-gnu)
    endif()
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

# `location` is the line and column the diagnostic names.
function(reject name location expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "${name}.x:${location}: error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1)
        message(FATAL_ERROR "${name}: missing '${location}: error: ${expected}'\n${diagnostics}")
    endif()
endfunction()

function(accept name source)
    compile(${name} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: unexpected rejection\n${diagnostics}")
    endif()
endfunction()

set(types "struct pair { i32 k; i32 l; };\nunion either { i32 a; u32 b; };\nstruct outer { struct pair t; union either u; i32 n; };\n")
set(duplicate "duplicate destination in aggregate initializer")
set(overlap "aggregate initializer entry overlaps the destination of an earlier entry")

# A value, then a part of it; the same aggregate or part twice is an error.
accept(part_after_value "${types}global i32 f(in struct pair x) {
    struct outer o = { .t = x, .t.l = 41, .n = 1 };
    return o.t.k + o.t.l;
}")
reject(whole_twice 5:32 "${duplicate}" "${types}global i32 f(in struct pair x, in struct pair y) {
    struct outer o = { .t = x, .t = y }; return o.n; }")
reject(part_twice 5:42 "${duplicate}" "${types}global i32 f(in struct pair x) {
    struct outer o = { .t = x, .t.l = 1, .t.l = 2 }; return o.n; }")
reject(part_before_whole 5:34 "${overlap}" "${types}global i32 f(in struct pair x) {
    struct outer o = { .t.l = 1, .t = x }; return o.n; }")
reject(part_after_list 5:39 "${overlap}" "${types}global i32 f() {
    struct outer o = { .t = { 1, 2 }, .t.l = 3 }; return o.n; }")
reject(union_second_member 5:42 "${overlap}" "${types}global i32 f(in union either v) {
    struct outer o = { .u = v, .u.a = 1, .u.b = 2 }; return o.n; }")
reject(union_two_members 5:32 "excess entry in union initializer" "${types}global i32 f() {
    union either e = { .a = 1, .b = 2 }; return e.a; }")
reject(file_part_twice 5:45 "${duplicate}" "${types}static const struct pair p = { 1, 2 };
global struct outer o = { .t = p, .t.k = 1, .t.k = 2 };")

# The base: only designated entries may follow it, and it has the list's type.
accept(base_then_designators "${types}global i32 f(in struct outer x) {
    struct outer o = { x, .t.l = 5, .u.b = 6u32 };
    return o.t.k + o.t.l + o.n;
}")
reject(positional_after_base 5:26 "excess entry in aggregate initializer" "${types}global i32 f(in struct pair x) {
    struct pair p = { x, 5 }; return p.l; }")
reject(positional_after_array_base 2:45 "excess entry in aggregate initializer" "global i32 f() {
    i32 a[3] = { 1, 2, 3 }; i32 b[3] = { a, 4 }; return b[0]; }")
reject(base_wrong_record 6:24 "a first entry of type 'struct pair' is neither a base of type 'struct other' nor a value of its first member"
    "${types}struct other { i32 k; i32 l; };\nglobal i32 f(in struct pair x) {
    struct other o = { x, .l = 1 }; return o.k; }")
reject(file_base_wrong_record 6:33 "a first entry of type 'const struct pair' is neither a base of type 'const struct other' nor a value of its first member"
    "${types}struct other { i32 k; i32 l; };\nstatic const struct pair p = { 1, 2 };
static const struct other o = { p, .l = 1 };")
reject(member_wrong_array 2:61 "aggregate initializer requires a brace list or an array of type 'i32[3]', not 'i32[2]'"
    "struct row { i32 v[3]; };\nglobal i32 f() { i32 a[2] = { 1, 2 }; struct row r = { .v = a }; return r.v[0]; }")
reject(array_declaration_value 1:47 "aggregate initializer requires a brace list"
    "global i32 f() { i32 a[4] = { 1 }; i32 b[4] = a; return b[0]; }")
reject(variable_length_base 1:60 "a variable-length array cannot take its initial value from an expression"
    "global i32 f(in i32 n) { i32 a[2] = { 1, 2 }; i32 v[n] = { a, [0] = 1 }; return v[0]; }")
reject(literal_positional_after_base 5:31 "excess entry in aggregate initializer" "${types}global i32 f(in struct pair x) {
    return ((struct pair){ x, 7 }).l; }")
# A base in a static initializer is a translation-time value.
reject(static_runtime_base 5:32 "runtime/static storage cannot be read during translation-time evaluation: 'p' is not const"
    "${types}static struct pair p = { 1, 2 };
static const struct pair q = { p, .l = 3 };")

# A u8 array's leading string may be followed by designated replacements.
accept(string_then_designators "global u8 f(in u8 c) {
    u8 s[8] = { \"abc\", [1] = c, [5] = 'z' };
    return s[1];
}
global u8 table[] = { \"xyz\", [0] = 'X' };
\$::static_assert(sizeof(table) == 4, \"bound from the string\");")
reject(string_then_positional 1:35 "excess entry in u8 array initializer"
    "global u8 f() { u8 s[4] = { \"ab\", 'c' }; return s[0]; }")
reject(string_then_outside 1:34 "array initializer designator is out of range"
    "global u8 f() { u8 s[] = { \"ab\", [3] = 'c' }; return s[0]; }")

# A base satisfies exhaustive.
accept(exhaustive_base "struct version [[exhaustive]] { u16 major; u16 minor; };
static const struct version current = { .major = 1, .minor = 4 };
global struct version next = { current, .minor = 5 };")
reject(exhaustive_without_base 2:30 "initializer of exhaustive record 'version' does not name member 'major'"
    "struct version [[exhaustive]] { u16 major; u16 minor; };\nglobal struct version next = { .minor = 5 };")

# The same rules on MIPS.
reject(mips_part_before_whole 5:34 "${overlap}" "${types}global i32 f(in struct pair x) {
    struct outer o = { .t.l = 1, .t = x }; return o.n; }" -target mips-unknown-elf)
accept(mips_base "${types}static const struct outer o = { .t = { 1, 2 }, .n = 3 };
global struct outer p = { o, .t.k = 4 };" -target mips-unknown-elf)
