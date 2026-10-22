# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# An `out` or `inout` argument is a modifiable lvalue, or `_` for `out`;
# `_` names nothing else.
foreach(required CC OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(prelude [=[
static void get(out i32 v) { v = 5; }
static void bump(inout i32 v) { v += 1; }
static T fill<T>(out T v) { v = (T)3; return v; }
struct Pair { i32 first; i32 second; };
global const i32 fixed = 1;
]=])
set(targets x86_64-unknown-linux-gnu mips-unknown-elf)

function(check name expected body)
    file(WRITE "${OUTPUT_DIR}/${name}.x" "${prelude}${body}\n")
    foreach(target IN LISTS targets)
        execute_process(COMMAND "${CC}" -target ${target} -S "${OUTPUT_DIR}/${name}.x"
            -o "${OUTPUT_DIR}/${name}-${target}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name} (${target}) rejected\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${name}[.]x:[0-9]+:[0-9]+: error: ${expected}")
            message(FATAL_ERROR "${name} (${target}) lacks '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

set(out_error "an 'out' argument must be a modifiable lvalue or '_'")
set(inout_error "an 'inout' argument must be a modifiable lvalue")
set(discard_error "'_' can only discard the result of an 'out' argument")
check(out_address "${out_error}" "global i32 f() { i32 x = 0; get(&x); return x; }")
check(out_constant "${out_error}" "global void f() { get(1); }")
check(out_rvalue "${out_error}" "global void f(i32 x) { get(x + 1); }")
check(out_const "${out_error}" "global void f() { get(fixed); }")
check(out_const_local "${out_error}" "global void f() { const i32 x = 0; get(x); }")
check(out_array "${out_error}" "global void f() { i32 a[1]; fill(a); }")
check(out_call "${out_error}" "static i32 g() { return 0; } global void f() { get(g()); }")
check(out_member_rvalue "${out_error}"
    "static struct Pair make() { struct Pair p = {1, 2}; return p; } global void f() { get(make().first); }")
check(out_unevaluated "${out_error}" "$::static_assert(sizeof(get(1)) == 0uptr, \"no\");")
check(inout_constant "${inout_error}" "global void f() { bump(0); }")
check(inout_rvalue "${inout_error}" "global void f(i32 x) { bump(x + 1); }")
check(inout_discard "${discard_error}" "global void f() { bump(_); }")
check(discard_value "${discard_error}" "global i32 f() { return _; }")
check(discard_parenthesized "${discard_error}" "global void f() { get((_)); }")
check(discard_assignment "${discard_error}" "global void f() { i32 x; x = _; }")
check(discard_object "'_' is the discard and cannot be a declared name" "global i32 _;")
check(discard_parameter "'_' is the discard and cannot be a declared name" "global void f(i32 _) {}")
check(discard_member "'_' is the discard and cannot be a declared name" "struct Padded { u8 _; };")
check(discard_label "'_' is the discard and cannot be a declared name" "global void f() { _: return; }")
check(discard_undeduced "could not deduce generic type parameter 'T'" "global void f() { fill(_); }")
check(accepted pass [=[
global i32 f(i32 x) {
    struct Pair pair;
    i32 values[2];
    get(_);
    get(pair.first);
    get(values[1]);
    get(*&x);
    bump((x));
    fill<i32>(_);
    $::static_assert(sizeof(fill<i32>(_)) == sizeof(i32), "an unevaluated discard");
    return x + pair.first + values[1];
}
]=])
