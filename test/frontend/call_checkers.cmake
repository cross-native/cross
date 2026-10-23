# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Call checkers written in Cross: [[check]] and [[checker]], the $::meta::call
# operations, the warning that controls a checker, and their constraints.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${ARGN}
        "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
    set(status "${result}" PARENT_SCOPE)
    set(err "${out}${error}" PARENT_SCOPE)
endfunction()

# A checker that reports what $::meta::call says about each call, so the
# test reads every operation's result from the messages.
set(reporter [=[
static $::meta::bytes digits(in uptr value) {
    $::meta::buffer buffer = $::meta::alloc(1);
    $::meta::data(buffer)[0] = (u8)('0' + value);
    return $::meta::freeze(buffer, 1);
}

static $::meta::bytes bytes(in const u8 *text) {
    uptr length = 0;
    while (text[length] != 0) ++length;
    $::meta::buffer buffer = $::meta::alloc(length);
    for (uptr i = 0; i < length; ++i) $::meta::data(buffer)[i] = text[i];
    return $::meta::freeze(buffer, length);
}

[[checker]]
static void report(in $::meta::call call) {
    $::meta::bytes summary = $::meta::concat(bytes("arguments "), digits($::meta::argument_count(call)));
    summary = $::meta::concat($::meta::concat(summary, bytes(" parameters ")), digits($::meta::parameter_count(call)));
    if ($::meta::is_variadic(call)) summary = $::meta::concat(summary, bytes(" variadic"));
    summary = $::meta::concat($::meta::concat(summary, bytes(" long ")),
                              $::meta::type_spelling($::meta::abi_type(call, "long")));
    $::meta::note($::meta::call_span(call), summary);
    for (uptr i = 0; i < $::meta::argument_count(call); ++i) {
        $::meta::bytes text = $::meta::concat(bytes("type "), $::meta::type_spelling($::meta::argument_type(call, i)));
        $::meta::bytes string = $::meta::argument_string(call, i);
        if ($::meta::len(string) != 0)
            text = $::meta::concat($::meta::concat(text, bytes(" string ")), digits($::meta::len(string)));
        $::meta::warning($::meta::argument_span(call, i), text);
    }
    $::meta::note($::meta::call_span(call), $::meta::type_spelling($::meta::parameter_type(call, 0)));
}

[[abi("sysv_abi"), check(report)]] i32 log(in const u8 *format, ...);
[[check(report)]] void plain(in u16 value);

global void entry(in i64 big, in u8 small, in u8 *buffer) {
    log("%ld", big, small, buffer, (const u8 *)"xy");
    plain(small);
}
]=])
compile(reporter "${reporter}")
foreach(expected
        "reporter.x:37:5: note: arguments 5 parameters 1 variadic long i64"
        "reporter.x:37:9: warning: type const u8 \\* string 4\n"
        "reporter.x:37:16: warning: type i64\n"
        "reporter.x:37:21: warning: type u8\n"
        "reporter.x:37:28: warning: type u8 \\*\n"
        "reporter.x:37:36: warning: type const u8 \\* string 3\n"
        "reporter.x:37:5: note: const u8 \\*"
        "reporter.x:38:5: note: arguments 1 parameters 1 long void"
        "reporter.x:38:11: warning: type u8\n"
        "reporter.x:38:5: note: u16")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "reporter: missing '${expected}'\n${err}")
    endif()
endforeach()
if(NOT status EQUAL 0)
    message(FATAL_ERROR "reporter: an uncategorized checker warning is not an error\n${err}")
endif()
compile(reporter_inhibited "${reporter}" -w)
if(NOT status EQUAL 0 OR err MATCHES "warning:")
    message(FATAL_ERROR "reporter_inhibited: -w suppresses checker warnings\n${err}")
endif()
compile(reporter_werror "${reporter}" -Werror)
if(NOT status EQUAL 1 OR NOT err MATCHES "reporter_werror.x:37:16: error: type i64 \\[-Werror\\]")
    message(FATAL_ERROR "reporter_werror: -Werror makes checker warnings errors\n${err}")
endif()

# A checker for a warning runs only while the warning is enabled.
set(keys [=[
static bool clean(in $::meta::bytes key) {
    for (uptr i = 0; i < $::meta::len(key); ++i)
        if ($::meta::at(key, i) == ' ') return 0;
    return 1;
}

[[checker("format")]]
static void key_checker(in $::meta::call call) {
    $::meta::bytes key = $::meta::argument_string(call, 0);
    if ($::meta::len(key) == 0)
        $::meta::warning($::meta::argument_span(call, 0), "the key is not a string literal");
    else if (!clean(key))
        $::meta::error($::meta::argument_span(call, 0), "a key cannot contain a space");
}

[[check(key_checker)]] u32 lookup(in const u8 *key);

global u32 entry(in const u8 *name) {
    return lookup("good") + lookup(name);
}
]=])
compile(keys_default "${keys}")
if(NOT status EQUAL 0 OR NOT err STREQUAL "")
    message(FATAL_ERROR "keys_default: a format checker runs only with -Wformat\n${err}")
endif()
compile(keys "${keys}" -Wall)
if(NOT status EQUAL 0 OR NOT err MATCHES "keys.x:19:36: warning: the key is not a string literal \\[-Wformat\\]")
    message(FATAL_ERROR "keys: missing the categorized warning\n${err}")
endif()
compile(keys_error "${keys}" -Werror=format)
if(NOT status EQUAL 1 OR NOT err MATCHES "error: the key is not a string literal \\[-Werror=format\\]")
    message(FATAL_ERROR "keys_error: -Werror=format\n${err}")
endif()
string(REPLACE "lookup(\"good\")" "lookup(\"bad key\")" spaced "${keys}")
compile(keys_space "${spaced}" -Wformat)
if(NOT status EQUAL 1 OR NOT err MATCHES "keys_space.x:19:19: error: a key cannot contain a space" OR
   NOT err MATCHES "note: while checking this call with the checker 'key_checker'")
    message(FATAL_ERROR "keys_space: a checker error is an error at the call\n${err}")
endif()

# Checks through generic instances are reported once.
compile(generic [=[
[[checker]]
static void always(in $::meta::call call) {
    $::meta::warning($::meta::call_span(call), "checked");
}
[[check(always)]] void sink(in u32 value);
static void relay<T>(in T value) { sink((u32)value); }
global void entry() { relay(1u8); relay(2u16); }
]=])
string(REGEX MATCHALL "warning: checked" reports "${err}")
list(LENGTH reports count)
if(NOT status EQUAL 0 OR NOT count EQUAL 1)
    message(FATAL_ERROR "generic: expected one report for both instances\n${err}")
endif()

# Every checker that a declaration of the function names checks each call.
compile(several [=[
[[checker]]
static void first(in $::meta::call call) {
    $::meta::warning($::meta::call_span(call), "first");
}
[[checker]]
static void second(in $::meta::call call) {
    $::meta::warning($::meta::call_span(call), "second");
}
[[check(first)]] void sink(in u32 value);
[[check(second), check(first)]] void sink(in u32 value);
global void entry() { sink(1); }
]=])
string(REGEX MATCHALL "warning: (first|second)" reports "${err}")
list(LENGTH reports count)
if(NOT status EQUAL 0 OR NOT count EQUAL 2 OR NOT err MATCHES "several.x:11:23: warning: first" OR
   NOT err MATCHES "several.x:11:23: warning: second")
    message(FATAL_ERROR "several: expected each checker once\n${err}")
endif()

# Calls in translation-only functions and in generated code are checked too.
compile(contexts [=[
[[checker]]
static void always(in $::meta::call call) {
    $::meta::warning($::meta::call_span(call), "checked");
}
[[check(always)]] static u32 twice(in u32 value) { return value * 2; }
[[eval_only]] static u32 four() { return twice(2); }
[[macro]]
static $::meta::tokens make(in $::meta::tokens input) {
    return $::quote { twice(3u32) };
}
global u32 a = four();
global u32 entry() { return make!() + twice(5); }
]=])
foreach(expected
        "contexts.x:6:42: warning: checked"
        "<expansion of 'make'>:1:1: warning: checked"
        "contexts.x:12:29: note: in expansion of procedural macro 'make'"
        "contexts.x:12:39: warning: checked")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "contexts: missing '${expected}'\n${err}")
    endif()
endforeach()

# Constraints.
foreach(case not_checker missing_argument bad_signature not_static unknown_warning group_warning on_object)
    if(case STREQUAL not_checker)
        set(source "static void helper(in $::meta::call call) {}\n[[check(helper)]] void f();\n")
        set(expected "check requires the name of a \\[\\[checker\\]\\] function; 'helper' is not one")
    elseif(case STREQUAL missing_argument)
        set(source "[[check]] void f();\n")
        set(expected "check requires the name of one \\[\\[checker\\]\\] function")
    elseif(case STREQUAL bad_signature)
        set(source "[[checker]] static void c(in $::meta::call call, in u32 extra) {}\n")
        set(expected "a \\[\\[checker\\]\\] function is a static, nongeneric definition")
    elseif(case STREQUAL not_static)
        set(source "[[checker]] void c(in $::meta::call call) {}\n")
        set(expected "function with a meta type in its signature must be static|a \\[\\[checker\\]\\] function is a static")
    elseif(case STREQUAL unknown_warning)
        set(source "[[checker(\"formats\")]] static void c(in $::meta::call call) {}\n")
        set(expected "checker names unknown warning 'formats'")
    elseif(case STREQUAL group_warning)
        set(source "[[checker(\"all\")]] static void c(in $::meta::call call) {}\n")
        set(expected "checker names unknown warning 'all'")
    else()
        set(source "[[check(c)]] global u32 value;\n")
        set(expected "attribute 'check' is valid only on a function")
    endif()
    compile(${case} "${source}")
    if(NOT status EQUAL 1 OR NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error: (${expected})")
        message(FATAL_ERROR "${case}: expected '${expected}'\n${err}")
    endif()
endforeach()
compile(unknown_shipped "[[check($::format::print)]] i32 f(in const u8 *format, ...);\n")
if(NOT status EQUAL 1 OR NOT err MATCHES "'\\$::format::print' is not one")
    message(FATAL_ERROR "unknown_shipped: a misspelled shipped checker\n${err}")
endif()

# File-scope calls run their checker before required evaluation folds them.
foreach(root initializer assertion)
    if(root STREQUAL initializer)
        set(expression "global u32 result = checked(1);")
    else()
        set(expression "$::static_assert(checked(1) == 1, \"value\");")
    endif()
    compile(file_scope_${root} "namespace scope {
[[checker]] static void reject(in $::meta::call call) {
    $::meta::error($::meta::call_span(call), \"file-scope call checked\");
}
[[check(reject)]] static u32 checked(in u32 value) { return value; }
${expression}
}
" -w)
    if(NOT status EQUAL 1 OR NOT err MATCHES "file_scope_${root}.x:6:[0-9]+: error: file-scope call checked")
        message(FATAL_ERROR "file_scope_${root}: expected the checker error before folding\n${err}")
    endif()
endforeach()

# Calls retain their written argument types in both kinds of file-scope roots.
compile(file_scope_arguments [=[
static u32 checked(in u8 value) { return value; }
global u32 result = checked(300);
$::static_assert(checked(300) == 44, "value");
]=] -Wconversion)
string(REGEX MATCHALL "warning: conversion from 'i32' to 'u8' changes value from '300' to '44'" reports "${err}")
list(LENGTH reports count)
if(NOT status EQUAL 0 OR NOT count EQUAL 2)
    message(FATAL_ERROR "file_scope_arguments: expected two conversions before folding\n${err}")
endif()
