# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Compound literals: a literal passed to a C function, parse forms, the public
# syntax tree, and the constraints of the declaration they mirror.
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

function(reject name expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1 OR
       NOT diagnostics MATCHES "${name}\\.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${name}: missing '${expected}'\n${diagnostics}")
    endif()
endfunction()

function(accept name source)
    compile(${name} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: unexpected rejection\n${diagnostics}")
    endif()
endfunction()

# A literal array passed to a C function, on both architectures.
set(reproducer [=[
[[abi("sysv_abi")]] i64 strtol(const u8 *s, u8 **end, i32 base);
global i64 parse(const u8 *p) {
    return strtol((u8[]){ p[0], p[1], p[2], p[3], 0 }, (u8 **)0, 16);
}
]=])
accept(reproducer_x86_64 "${reproducer}")
string(REPLACE "[[abi(\"sysv_abi\")]] " "" mips_reproducer "${reproducer}")
accept(reproducer_mips "${mips_reproducer}" -target mips-unknown-elf)

# `sizeof (T){...}` measures the literal, and casts still parse.
accept(forms [=[
struct point { i32 x; i32 y; };
$::static_assert(sizeof (u8[]){ 1, 2, 3 } == 3uptr, "sizeof of a literal");
$::static_assert($::alignof((struct point){ 1, 2 }) == $::alignof(struct point), "alignof");
global i32 cast_then_literal(in u32 v) {
    return (i32)(u32)v + (i32)((struct point){ 1, 2 }).y + ((i32[2]){ 3, 4 })[1];
}
global i32 updated() {
    (struct point){ 1, 2 }.x = 7;
    return ++((i32[]){ 4 })[0];
}
]=])

# A captured expression keeps a compound_literal node in its public tree.
accept(public_tree [=[
static u32 has_literal(in $::meta::syntax node) {
    if ($::meta::is_production(node, "compound_literal")) return 1u32;
    for (uptr i = 0uptr; i < $::meta::child_count(node); ++i)
        if (has_literal($::meta::child(node, i)) != 0u32) return 1u32;
    return 0u32;
}
[[syntax_expander]]
static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    if (has_literal(value) == 0u32) return $::quote { missing_compound_literal };
    return $::quote { $::unquote(value) + 1u32 };
}
syntax Bump : expression { prefix "bump"; match value:expr; expand expand; }
syntax Bump;
global u32 entry() { return bump ((u32[]){ 1u32, 2u32 })[1]; }
$::static_assert(bump ((u32[]){ 4u32 })[0] == 5u32, "expanded literal");
]=])

set(record "struct point { i32 x; i32 y; };\nstruct other { i32 x; };\n")
reject(scalar "brace initializer requires an aggregate destination"
    "global u32 f() { return (u32){ 5u32 }; }")
reject(named "a compound literal type name cannot declare an object"
    "global u32 f() { return (u32 named[]){ 5u32 }[0]; }")
reject(incomplete "local object requires a complete record type"
    "struct missing;\nglobal uptr f() { return sizeof((struct missing){ 0 }); }")
reject(empty_inferred "an omitted array bound requires a nonempty initializer"
    "global u8 *f() { return (u8[]){}; }")
reject(const_write "cannot write a const subobject"
    "global void f() { ((const i32[]){ 1 })[0] = 2; }")
reject(record_type "a record value requires the same nominal record type"
    "${record}global i32 f() { struct other o = (struct point){ 1, 2 }; return o.x; }")
reject(excess "array initializer designator is out of range"
    "global i32 f() { return ((i32[1]){ 1, 2 })[0]; }")
reject(runtime_bound "runtime local or parameter is not a translation-time value"
    "global i32 f(in u32 n) { return ((i32[n]){ 1 })[0]; }")
# A block-scope literal has automatic storage, even in a static initializer.
reject(static_block_address "a compound literal in a block has automatic storage; its address is not a static initializer"
    "global i32 *f() { static i32 *saved = (i32[]){ 1, 2 }; return saved; }")
