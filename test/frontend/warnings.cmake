# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Selectable warnings: each warning with and without its cause, the groups,
# -Werror and its forms, -w, unknown options, profiles, [[maybe_unused]], and
# [[fallthrough]]. warning_output.cmake checks that warnings leave the output
# unchanged.
foreach(required CC CPP OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

# compile(CASE SOURCE FLAGS...) sets `status` and `err`.
function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${ARGN}
        "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
    set(status "${result}" PARENT_SCOPE)
    set(err "${out}${error}" PARENT_SCOPE)
endfunction()

# The expect functions also set `err` for further checks.
function(expect case source pattern)
    compile(${case} "${source}" ${ARGN})
    if(NOT status EQUAL 0 OR NOT err MATCHES "${pattern}")
        message(FATAL_ERROR "${case}: expected '${pattern}'\n${err}")
    endif()
    set(err "${err}" PARENT_SCOPE)
endfunction()

# Fails unless `err` holds COUNT warnings.
function(expect_count case count)
    string(REGEX MATCHALL "warning:" reports "${err}")
    list(LENGTH reports total)
    if(NOT total EQUAL count)
        message(FATAL_ERROR "${case}: expected ${count} warnings, got ${total}\n${err}")
    endif()
endfunction()

function(expect_clean case source)
    compile(${case} "${source}" ${ARGN})
    if(NOT status EQUAL 0 OR NOT err STREQUAL "")
        message(FATAL_ERROR "${case}: expected no diagnostic\n${err}")
    endif()
endfunction()

function(expect_error case source pattern)
    compile(${case} "${source}" ${ARGN})
    if(NOT status EQUAL 1 OR NOT err MATCHES "${pattern}")
        message(FATAL_ERROR "${case}: expected error '${pattern}'\n${err}")
    endif()
    set(err "${err}" PARENT_SCOPE)
endfunction()

# --- -Wunused-variable ---

set(unused_local [=[
global u32 f(in u32 a) {
    u32 spare = a;
    return 1;
}
]=])
expect(unused_local "${unused_local}"
    "unused_local.x:2:9: warning: unused variable 'spare' \\[-Wunused-variable\\]" -Wunused-variable)
expect_clean(unused_local_default "${unused_local}")
expect_clean(unused_local_marked [=[
global u32 f(in u32 a) {
    [[maybe_unused]] u32 spare = a;
    u32 used = a;
    (void)used;
    return 1;
}
]=] -Wall)
expect(unused_block_static [=[
global u32 f() {
    static u32 calls;
    return 1;
}
]=] "unused_block_static.x:2:16: warning: unused variable 'calls'" -Wall)
file(WRITE "${OUTPUT}/table.xh" "static const u32 header_table[2] = { 1, 2 };\n")
expect(unused_static_object [=[
#include "table.xh"
static u32 counter;
static const u32 table[1] = { 3 };
[[maybe_unused]] static u32 spare;
static u32 used = 4;
global u32 read() { return used; }
]=] "unused_static_object.x:2:12: warning: 'counter' defined but not used \\[-Wunused-variable\\]" -Wall)
if(NOT err MATCHES "unused_static_object.x:3:18: warning: 'table' defined but not used")
    message(FATAL_ERROR "unused_static_object: a const object in the input file\n${err}")
endif()
# Not header_table, which an included file defines, nor spare or used.
expect_count(unused_static_object 2)

# --- -Wunused-parameter ---

set(unused_parameter [=[
global u32 f(in u32 used, in u32 spare, in u32, in u32 [[maybe_unused]] marked, out u32 result) {
    result = used;
    return used;
}
[[macro]]
static $::meta::tokens constant(in $::meta::tokens input) {
    return $::quote { 1u32 };
}
global u32 g() { return constant!(); }
]=])
expect(unused_parameter "${unused_parameter}"
    "unused_parameter.x:1:34: warning: unused parameter 'spare' \\[-Wunused-parameter\\]" -Wall -Wextra)
# Not the unnamed, marked, or output parameter, nor the macro's input.
expect_count(unused_parameter 1)
expect(unused_parameter_unused "${unused_parameter}" "unused parameter 'spare'" -Wunused -Wextra)
# As in GCC, -Wextra alone does not enable it: -Wunused must be on too.
expect_clean(unused_parameter_extra "${unused_parameter}" -Wextra)
expect_clean(unused_parameter_all "${unused_parameter}" -Wall)
# A naked function reads its parameters through their locations.
expect_clean(unused_parameter_naked [=[
[[naked]] global void f(in u32 value "edi") {
    $::_ret();
}
]=] -Wall -Wextra)

# --- -Wunused-function ---

set(unused_function [=[
static u32 spare() { return 1; }
static u32 called() { return 2; }
static u32 square(in u32 x) { return x * x; }
static u32 addressed() { return 3; }
static inline u32 hinted() { return 4; }
[[maybe_unused]] static u32 marked() { return 5; }
[[used]] static u32 kept() { return 6; }
static $::meta::tokens helper(in $::meta::tokens input) { return input; }
global u32 answer = square(6);
global u32 (*pointer)() = addressed;
global u32 entry() { return called(); }
]=])
expect(unused_function "${unused_function}"
    "unused_function.x:1:12: warning: 'spare' defined but not used \\[-Wunused-function\\]" -Wall)
expect_count(unused_function 1)
expect_clean(unused_function_default "${unused_function}")
# An alias names the definition it aliases by link name.
expect(unused_alias [=[
[[link_name("cross_target")]] static u32 target() { return 7; }
[[alias("cross_target")]] global u32 exported();
static u32 lonely() { return 8; }
]=] "unused_alias.x:3:12: warning: 'lonely' defined but not used" -Wall)
expect_count(unused_alias 1)
expect_clean(unused_function_disabled "${unused_function}" -Wall -Wno-unused-function)

# --- -Wimplicit-fallthrough and [[fallthrough]] ---

set(fallthrough [=[
[[noreturn]] void stop();
global u32 f(in u32 x) {
    switch (x) {
    case 0:
        x = 1;
    case 1:
        x = 2;
        break;
    case 2:
        x = 3;
        [[fallthrough]];
    case 3:
    case 4:
        if (x > 3) return 1; else return 2;
    case 5:
        stop();
    case 6:
        for (;;) {}
    case 7:
        $::unreachable();
    case 8:
        if (x == 8) { x = 9; [[fallthrough]]; } else { return 3; }
    case 9:
        while (1) { if (x == 0) break; }
    default:
        break;
    }
    return x;
}
]=])
expect(fallthrough "${fallthrough}"
    "fallthrough.x:5:9: warning: this statement may fall through \\[-Wimplicit-fallthrough\\]" -Wextra)
if(NOT err MATCHES "fallthrough.x:6:5: note: here" OR
   NOT err MATCHES "fallthrough.x:24:9: warning: this statement may fall through")
    message(FATAL_ERROR "fallthrough: missing note or the loop that breaks\n${err}")
endif()
string(REGEX MATCHALL "may fall through" reports "${err}")
list(LENGTH reports count)
if(NOT count EQUAL 2)
    message(FATAL_ERROR "fallthrough: expected two reports\n${err}")
endif()
expect_clean(fallthrough_default "${fallthrough}")
# A fallthrough statement in an if without else, or before the first label,
# is placed well; the if itself may still fall through.
set(fallthrough_if [=[
global u32 f(in u32 x) {
    switch (x) {
    [[fallthrough]];
    case 0:
        if (x == 0) [[fallthrough]];
    case 1:
        return 1;
    }
    return 0;
}
]=])
expect(fallthrough_if "${fallthrough_if}" "fallthrough_if.x:5:9: warning: this statement may fall through" -Wextra)
expect_count(fallthrough_if 1)
expect_clean(fallthrough_if_default "${fallthrough_if}")
expect_error(fallthrough_outside [=[
global void f() {
    [[fallthrough]];
}
]=] "fallthrough_outside.x:2:7: error: a fallthrough statement must precede a case or default label")
expect_error(fallthrough_not_last [=[
global void f(in u32 x) {
    switch (x) {
    case 0:
        [[fallthrough]];
        x = 1;
    case 1:
        break;
    }
}
]=] "fallthrough_not_last.x:4:11: error: a fallthrough statement must precede")
expect_error(fallthrough_end [=[
global void f(in u32 x) {
    switch (x) {
    case 0:
        x = 1;
        [[fallthrough]];
    }
}
]=] "fallthrough_end.x:5:11: error: a fallthrough statement must precede")
expect_error(fallthrough_statement [=[
global void f(in u32 x) {
    switch (x) {
    case 0:
        [[fallthrough]] x = 1;
    case 1:
        break;
    }
}
]=] "fallthrough applies only to an empty statement")
expect_error(fallthrough_arguments [=[
global void f(in u32 x) {
    switch (x) {
    case 0:
        [[fallthrough(1)]];
    case 1:
        break;
    }
}
]=] "fallthrough does not take arguments")
expect_error(fallthrough_twice [=[
global void f(in u32 x) {
    switch (x) {
    case 0:
        [[fallthrough, fallthrough]];
    case 1:
        break;
    }
}
]=] "fallthrough_twice.x:4:24: error: an empty statement has at most one fallthrough attribute")
expect_error(maybe_unused_record [=[
struct point [[maybe_unused]] { u32 x; };
]=] "attribute 'maybe_unused' is valid only on a function, object, or parameter")

# --- -Wsign-compare ---

set(sign_compare [=[
global bool f(in i32 a, in u32 b, in u8 c, in i64 d, in iptr m, in uptr n) {
    bool r = a < b;
    r = m >= n;
    r = c < b;
    r = b > 0;
    r = (a & 0xff) < b;
    r = d < b;
    r = b == 5;
    r = (u32)a < b;
    return r;
}
]=])
expect(sign_compare "${sign_compare}"
    "sign_compare.x:2:16: warning: comparison of integer expressions of different signedness: 'i32' and 'u32' \\[-Wsign-compare\\]"
    -Wextra)
if(NOT err MATCHES "sign_compare.x:3:11: warning: comparison of integer expressions of different signedness: 'iptr' and 'uptr'")
    message(FATAL_ERROR "sign_compare: missing the pointer-width comparison\n${err}")
endif()
string(REGEX MATCHALL "different signedness" reports "${err}")
list(LENGTH reports count)
if(NOT count EQUAL 2)
    message(FATAL_ERROR "sign_compare: expected two reports\n${err}")
endif()
expect_clean(sign_compare_all "${sign_compare}" -Wall)

# --- -Wconversion, -Wsign-conversion, -Wfloat-conversion ---

set(conversion [=[
void take(in u8 value);
global u8 f(in u32 v, in i32 s, in f64 d, in u8 x, in u8 y, in i64 l) {
    u8 a = v;
    take(v);
    u8 b = 300;
    u32 c = -1;
    u32 e = s;
    i32 g = d;
    f32 h = d;
    f32 k = 0.1;
    f64 m = l;
    a = v;
    u8 ok1 = 200;
    u8 ok2 = x + y;
    u8 ok3 = v & 0xff;
    u8 ok4 = (u8)v;
    u8 ok5 = 1 << 7;
    f32 ok6 = 0.5;
    i32 ok7 = 2.0;
    bool ok8 = v;
    i64 ok9 = v;
    a += v;
    return a;
}
]=])
expect(conversion "${conversion}"
    "conversion.x:3:12: warning: conversion from 'u32' to 'u8' may change value \\[-Wconversion\\]" -Wconversion)
foreach(expected
        "conversion.x:4:10: warning: conversion from 'u32' to 'u8' may change value"
        "conversion.x:5:12: warning: conversion from 'i32' to 'u8' changes value from '300' to '44' \\[-Wconversion\\]"
        "conversion.x:6:13: warning: conversion from 'i32' to 'u32' changes value from '-1' to '4294967295' \\[-Wsign-conversion\\]"
        "conversion.x:7:13: warning: conversion to 'u32' from 'i32' may change the sign of the result \\[-Wsign-conversion\\]"
        "conversion.x:8:13: warning: conversion from 'f64' to 'i32' may change value \\[-Wfloat-conversion\\]"
        "conversion.x:9:13: warning: conversion from 'f64' to 'f32' may change value \\[-Wfloat-conversion\\]"
        "conversion.x:10:13: warning: conversion from 'f64' to 'f32' changes the value of a constant"
        "conversion.x:11:13: warning: conversion from 'i64' to 'f64' may change value \\[-Wconversion\\]"
        "conversion.x:12:9: warning: conversion from 'u32' to 'u8' may change value")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "conversion: missing '${expected}'\n${err}")
    endif()
endforeach()
if(err MATCHES "conversion.x:(1[3-9]|2[0-9]):")
    message(FATAL_ERROR "conversion: a conversion that keeps its value was reported\n${err}")
endif()
compile(conversion_narrow "${conversion}" -Wconversion -Wno-sign-conversion -Wno-float-conversion)
if(NOT status EQUAL 0 OR err MATCHES "sign-conversion|float-conversion" OR NOT err MATCHES "\\[-Wconversion\\]")
    message(FATAL_ERROR "conversion_narrow: -Wno-sign-conversion and -Wno-float-conversion\n${err}")
endif()
# -Wall and -Wextra do not enable -Wconversion; the locals are unused.
expect_clean(conversion_default "${conversion}" -Wall -Wextra -Wno-unused-variable)

# --- Groups, -Werror, -w, and unknown options ---

expect_clean(order_independent "${unused_local}" -Wno-unused-variable -Wall)
expect_clean(group_off "${unused_local}" -Wall -Wno-unused)
expect_clean(all_off "${unused_local}" -Wall -Wno-all)
expect_clean(inhibit "${unused_local}" -Wall -Werror -w)
expect_error(werror "${unused_local}"
    "werror.x:2:9: error: unused variable 'spare' \\[-Werror=unused-variable\\]" -Wall -Werror)
if(NOT err MATCHES "cc: note: all warnings being treated as errors")
    message(FATAL_ERROR "werror: missing the -Werror note\n${err}")
endif()
expect_error(werror_one "${unused_local}" "error: unused variable 'spare' \\[-Werror=unused-variable\\]"
    -Werror=unused-variable)
if(err MATCHES "all warnings being treated")
    message(FATAL_ERROR "werror_one: -Werror=NAME is not -Werror\n${err}")
endif()
expect_error(werror_group "${unused_local}" "\\[-Werror=unused-variable\\]" -Werror=unused)
expect(werror_exempt "${unused_local}" "warning: unused variable 'spare' \\[-Wunused-variable\\]"
    -Wall -Werror -Wno-error=unused-variable)
expect_error(unknown_warning "${unused_local}" "cc: error: unknown warning option '-Wunused-varible' from -Wunused-varible; did you mean '-Wunused-variable'\\?"
    -Wunused-varible)
expect_clean(unknown_negative_quiet "${unused_local}" -Wno-missing-braces)
expect(unknown_negative_noted "${unused_local}"
    "cc: note: unrecognized command-line option '-Wno-missing-braces' may have been intended to silence earlier diagnostics"
    -Wall -Wno-missing-braces)
expect_error(unknown_werror "${unused_local}" "unknown warning option '-Wmissing-braces'" -Werror=missing-braces)
expect_error(unknown_no_werror "${unused_local}" "unknown warning option '-Wmissing-braces' in '-Wno-error=missing-braces'"
    -Wno-error=missing-braces)
expect_error(warning_value "${unused_local}" "warning option '-Wformat=2' takes no value" -Wformat=2)

# The default warnings are selectable too.
set(directive "#warning check this\nglobal u32 f() { return 1; }\n")
expect(cpp_default "${directive}" "cpp_default.x:1:1: warning: #warning check this \\[-Wcpp\\]")
expect_clean(cpp_disabled "${directive}" -Wno-cpp)
expect_error(cpp_error "${directive}" "error: #warning check this \\[-Werror=cpp\\]" -Werror)
file(WRITE "${OUTPUT}/directive.x" "${directive}")
foreach(flag -Wno-cpp -Werror)
    execute_process(COMMAND "${CPP}" ${flag} "${OUTPUT}/directive.x" -o "${OUTPUT}/directive${flag}.i"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if((flag STREQUAL "-Wno-cpp" AND (NOT result EQUAL 0 OR NOT error STREQUAL "")) OR
       (flag STREQUAL "-Werror" AND (NOT result EQUAL 1 OR NOT error MATCHES "\\[-Werror=cpp\\]")))
        message(FATAL_ERROR "cpp ${flag}: unexpected result ${result}\n${error}")
    endif()
endforeach()
set(packed [=[
struct record [[packed]] { u8 tag; u32 value; };
global u32 *member(in struct record *r) { return &r->value; }
]=])
expect(packed_default "${packed}" "\\[-Waddress-of-packed-member\\]")
expect_clean(packed_disabled "${packed}" -Wno-address-of-packed-member)

# A profile sets warnings like other options.
file(WRITE "${OUTPUT}/profile.y" [=[
profile "strict" {
    target = "x86_64-unknown-linux-gnu";
    abi = "x";
    w.all = true;
    w.error = true;
}
]=])
compile(profile "${unused_local}" "--model=${OUTPUT}/profile.y" -mprofile=strict)
if(NOT status EQUAL 1 OR NOT err MATCHES "\\[-Werror=unused-variable\\]")
    message(FATAL_ERROR "profile: w.all and w.error were not applied\n${err}")
endif()
expect_clean(profile_override "${unused_local}" "--model=${OUTPUT}/profile.y" -mprofile=strict -Wno-unused-variable)
execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -Wall --print-options=warning
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR
   NOT out MATCHES "w.unused-parameter  type: boolean  value: off  origin: default[^\n]*enabled_by: -Wunused and -Wextra" OR
   NOT out MATCHES "w.unused-variable  type: boolean  value: on  origin: command-line[^\n]*source: -Wall" OR
   out MATCHES "f\\.tree-ccp")
    message(FATAL_ERROR "--print-options=warning\n${out}${error}")
endif()

# File-scope scalar conversions use the same rules as local initializers.
set(global_conversions [=[
namespace values {
global u8 narrow = 300;
global u32 sign = -1;
global f32 fraction = 1.23456789;
global u8 fits = 200;
}
]=])
expect(global_conversions "${global_conversions}" "conversion from 'i32' to 'u8' changes value from '300' to '44'" -Wconversion)
foreach(expected "\\[-Wconversion\\]" "\\[-Wsign-conversion\\]" "\\[-Wfloat-conversion\\]")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "global_conversions: missing '${expected}'\n${err}")
    endif()
endforeach()
expect_count(global_conversions 3)
expect_clean(global_conversions_disabled "${global_conversions}" -Wno-conversion)
expect_error(global_conversions_error "${global_conversions}" "\\[-Werror=conversion\\]" -Werror=conversion)

# maybe_unused changes diagnostics, including for translation-only cells.
expect_clean(unused_meta_cell [=[
[[eval_only]] static u32 helper() {
    [[maybe_unused]] $::meta::buffer b = $::meta::alloc(1);
    return 7;
}
global u32 result = helper();
]=] -Wall -Werror)

# A function's own references do not use its static definition.
set(recursive_statics [=[
namespace recursion {
static u32 lonely(in u32 n);
static u32 lonely(in u32 n) { return n ? recursion::lonely(n - 1) : 0; }
static u32 addressed(in u32 n) {
    u32 (*self)(in u32) = addressed;
    return n ? self(n - 1) : 0;
}
static u32 called(in u32 n) { return n ? called(n - 1) : 0; }
global u32 entry(in u32 n) { return called(n); }
}
]=])
expect(recursive_statics "${recursive_statics}" "'recursion::lonely' defined but not used" -Wunused-function)
if(NOT err MATCHES "'recursion::addressed' defined but not used")
    message(FATAL_ERROR "recursive_statics: a self-address does not use the function\n${err}")
endif()
expect_count(recursive_statics 2)
expect_clean(recursive_statics_disabled "${recursive_statics}" -Wno-unused-function)
