# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Attribute brackets follow C23: two consecutive '[' tokens open an attribute
# where one may appear and are an error elsewhere, so ']]' may close two
# subscripts. Spacing never matters, in source, quotes, or macro input.
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

# Subscripts that close with ']]', also more deeply nested, on both
# architectures.
set(nested [=[
global u32 nested(const u32 *a, const u32 *b, u32 i) {
    return a[b[i]];
}
global u32 deep(u32 m[4][4], const u32 *b, u32 i) {
    return m[b[i]][b[b[i]]] + m[b[b[b[i]]]][0];
}
global void store(u32 *a, const u32 *b, u32 i) {
    a[b[i]] = a[b[b[i]]] + 1u32;
}
]=])
accept(nested_x86_64 "${nested}")
accept(nested_mips "${nested}" -target mips-unknown-elf)
file(READ "${OUTPUT}/nested_x86_64.s" assembly)
if(NOT assembly MATCHES "nested:")
    message(FATAL_ERROR "nested subscripts emitted no function\n${assembly}")
endif()

# Attribute specifiers keep every position, with or without spaced brackets.
accept(spaced_attributes [=[
[ [abi("sysv_abi")] ] i32 c_puts(const u8 *s);
typedef u32 word [ [aligned(8)] ];
struct packed_pair [ [packed] ] { u8 a; u32 b; };
struct member_aligned { u8 a; u32 b [ [aligned(8)] ]; };
$::static_assert(sizeof(word) == 8uptr, "typedef attribute");
$::static_assert(sizeof(struct packed_pair) == 5uptr, "tag attribute");
$::static_assert($::offsetof(struct member_aligned, b) == 8uptr, "member attribute");
[ [abi("sysv_abi")] ] {
    i32 c_close(in i32 fd);
}
static u32 helper(in u32 x) [ [noinline] ] { return x + 1u32; }
global u32 tail(in u32 x) {
    [ [musttail] ] return helper(x);
}
global u32 local(in u32 x) {
    u32 cell [ [aligned(16)] ] = x;
    return cell;
}
]=])

# Macro input, quotes, and token inspection see the same brackets.
accept(macro_brackets [=[
[[macro]]
static $::meta::tokens first($::meta::tokens input) {
    return $::meta::children($::meta::at(input, 0uptr));
}
[[macro]]
static $::meta::tokens aligned_word($::meta::tokens input) {
    return $::quote { typedef u32 $::unquote(input) [ [aligned(8)] ]; };
}
aligned_word!(quoted_word)
$::static_assert($::alignof(quoted_word) == 8uptr, "quoted attribute");
static $::meta::tokens delimiter_check(in $::meta::tokens input) {
    $::static_assert($::meta::delimiter($::quote { [ [x] ] })[1uptr] == '[', "attribute group");
    $::static_assert($::meta::delimiter($::quote { [x] })[1uptr] == ']', "bracket group");
    $::static_assert($::meta::len($::meta::parse("a[b[i]]")) == 2uptr, "nested subscripts");
    $::static_assert($::meta::len($::meta::parse("[[x]] [ [y] ]")) == 2uptr, "two attribute groups");
    return input;
}
[[macro]]
static $::meta::tokens checked($::meta::tokens input) { return delimiter_check(input); }
global u32 apply(const u32 *a, const u32 *b) {
    return first![[a[b[0]] + 1u32], [2u32]] + checked!(a[b[b[0]]]);
}
]=])

# Two consecutive '[' tokens where no attribute may appear.
set(postfix "two consecutive '[' tokens introduce an attribute, which cannot follow an expression")
set(primary "two consecutive '[' tokens introduce an attribute, which cannot begin an expression")
reject(subscript_attribute "${postfix}"
    "global u32 f(const u32 *a, u32 i) { return a[[i]]; }")
reject(spaced_subscript_attribute "${postfix}"
    "global u32 f(const u32 *a, u32 i) { return a[ [i] ]; }")
reject(expression_attribute "${primary}"
    "global u32 f(u32 i) { u32 x = [[i]]; return x; }")
reject(argument_attribute "${primary}"
    "static u32 g(u32 v) { return v; }\nglobal u32 f(u32 i) { return g([[i]]); }")
