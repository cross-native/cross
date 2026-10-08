# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()

function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL "pass")
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
            file(READ "${OUTPUT}/${name}-${level}.s" assembly)
            if(assembly MATCHES "helper|check_tokens|apply|expand_checked|unused|recurse")
                message(FATAL_ERROR "${name}/${level}: translation-only definition leaked\n${assembly}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

check(invocations pass [=[
[[eval_only]] static u32 helper(in u32 value) {
    u32 local = value + 1u32;
    $::static_assert(local == value + 1u32, "invocation locals");
    for (u32 index = 0u32; index < value; ++index) {
        $::static_assert(index < value, "loop state");
    }
    if (value == 0u32) $::static_assert(0u32, "untaken branch");
    $::static_assert(1.5f64, "floating scalar");
    return local;
    $::static_assert(0u32, "unreached assertion");
}
[[eval_only]] static T recurse<T>(in T value) {
    $::static_assert(value != (T)0u32, "recursive argument");
    if (value == (T)1u32) return value;
    return (T)1u32 + recurse<T>(value - (T)1u32);
}
[[eval_only]] static u32 unused() { $::static_assert(0u32, "unused body"); return 0u32; }
static uptr check_tokens(in $::meta::tokens input) {
    uptr length = $::meta::len(input);
    $::static_assert(length > 0uptr, "token input");
    return length;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::static_assert(check_tokens(input) == 2uptr, "nested helper");
    $::static_assert(helper(2u32) == 3u32, "macro scalar helper");
    return input;
}
[[syntax_expander]] static $::meta::tokens expand_checked(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::static_assert(!$::meta::is_kind(value, "deferred"), "captured node");
    return $::quote { $::unquote(value) };
}
syntax Checked : expression { prefix "checked"; match "(" value:expr ")"; expand expand_checked; }
syntax Checked;
$::static_assert(helper(2u32) == 3u32 && helper(5u32) == 6u32, "distinct calls");
$::static_assert(recurse<u32>(3u32) == 3u32 && recurse<u16>(4u16) == 4u16, "generic calls");
$::static_assert(apply!(checked(7u32)) == 7u32, "macro and syntax assertions");
global u32 entry() { return helper(6u32); }
]=])

check(second_invocation "static_assert failed: second call" [=[
[[eval_only]] static u32 helper(in u32 value) {
    $::static_assert(value == 1u32, "second call"); return value;
}
$::static_assert(helper(1u32) == 1u32, "first succeeds");
$::static_assert(helper(2u32) == 2u32, "second must recheck");
]=])
check(captured_assertions pass [=[
[[syntax_expander]] static $::meta::tokens expand_checked(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Keep : statement { prefix "keep"; match body:declaration; expand expand_checked; }
syntax Drop : statement { prefix "drop"; match body:declaration; expand drop; }
[[eval_only]] static u32 helper(in u32 value) {
    syntax Keep, Drop;
    keep $::static_assert(value == 7u32, "destination invocation frame");
    drop $::static_assert(0u32, "discarded capture");
    return value;
}
global u32 entry() { return helper(7u32); }
]=])
check(generic_meta_failure "static_assert failed: generic meta invocation" [=[
static T helper<T>(in T value) {
    $::static_assert($::meta::len(value) != 0uptr, "generic meta invocation"); return value;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(); }
]=])
check(loop_reached "static_assert failed: third iteration" [=[
[[eval_only]] static u32 helper() {
    for (u32 index = 0u32; index < 3u32; ++index)
        $::static_assert(index < 2u32, "third iteration");
    return 0u32;
}
global u32 entry() { return helper(); }
]=])
check(meta_failure "static_assert failed: empty tokens" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    $::static_assert($::meta::len(input) != 0uptr, "empty tokens"); return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(); }
]=])
check(uninitialized "uninitialized" [=[
[[eval_only]] static u32 helper() {
    u32 value; $::static_assert(value, "uninitialized state"); return 1u32;
}
global u32 entry() { return helper(); }
]=])
check(unused_bad_type "static_assert condition must have a scalar arithmetic type" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::static_assert(input, "invalid even when unreached"); return input;
}
global u32 entry() { return 1u32; }
]=])
check(untaken_bad_name "not visible|unresolved|unknown" [=[
[[eval_only]] static u32 helper() {
    if (0u32) $::static_assert(missing_value, "invalid name"); return 1u32;
}
global u32 entry() { return helper(); }
]=])
check(ordinary_from_macro "constant|uninitialized|runtime" [=[
static u32 helper(in u32 value) {
    $::static_assert(value == 1u32, "ordinary remains declaration-time"); return value;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::static_assert(helper(1u32) == 1u32, "caller is translation-only"); return input;
}
global u32 entry() { return apply!(1u32); }
]=])
check(resource_limit "translation-time instruction budget exceeded 200" [=[
[[eval_only]] static u32 helper() {
    for (u32 index = 0u32; index < 1000u32; ++index)
        $::static_assert(index < 1000u32, "bounded work");
    return 1u32;
}
global u32 entry() { return helper(); }
]=] -feval-step-limit=200)
