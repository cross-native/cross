# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags -fno-eval-calls)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
set(prefix [=[
typedef u8 T;
[[macro]] static $::meta::tokens part(in $::meta::tokens input) {
    $::meta::note($::meta::span($::quote { 0 }), "middle-fragment");
    return input;
}
[[macro]] static $::meta::tokens last(in $::meta::tokens input) {
    $::meta::note($::meta::span($::quote { 0 }), "last-fragment");
    return input;
}
[[syntax_expander]] static $::meta::tokens early(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), "first-owner");
    return $::quote { 16u32 };
}
syntax Early : expression { prefix "early"; match input:paren; expand early; }
syntax Early;
]=])
function(check source)
    string(MD5 case "${source}")
    file(WRITE "${OUTPUT}/${case}.x" "${prefix}\n${source}")
    execute_process(COMMAND "${CC}" -S ${flags} "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case}: unexpected result ${status}\n${out}\n${err}")
    endif()
    set(previous -1)
    foreach(marker first-owner middle-fragment last-fragment)
        string(FIND "${err}" "note: ${marker}" position)
        string(REGEX MATCHALL "note: ${marker}" occurrences "${err}")
        list(LENGTH occurrences count)
        if(NOT count EQUAL 1 OR NOT position GREATER previous)
            message(FATAL_ERROR "${case}: ${marker} missing, repeated or out of order\n${err}")
        endif()
        set(previous ${position})
    endforeach()
endfunction()
foreach(header
    "part!(T) test(in T value)"
    "T part!(test)(in T value)"
    "T (part!(test))(in T value)"
    "T part!((test))(in T value)"
    "T part!(test(in T value))"
    "T test part!((in T value))"
    "T test(in part!(T) value)"
    "T test(in T part!(value))"
    "T test(in T (part!(value)))"
    "T test(part!(in T value))"
    "T test(in T value) part!()"
    "T test(in T value) [[part!(noinline)]]"
    "T part!() test(in T value)"
    "part!(const) T test(in T value)"
    "T part!(const) test(in T value)"
    "T test(in T value) part!([[noinline]])")
    check("[[aligned(early(unknown!()))]] static ${header} [[generic(last!(T))]] { return value; }
$::static_assert(test(300u32) == 300u32, \"generic binding lost\");")
endforeach()
foreach(header
    "T part!(*)test(in T *value)"
    "T *part!(const) test(in T *value)"
    "T (*part!(test)(in T *value))"
    "T (*test(part!(in T *value)))"
    "T (*test(in T *value))part!()")
    check("[[aligned(early(unknown!()))]] static ${header} [[generic(last!(T))]] { return value; }
global uptr entry() { return sizeof(&test<u32>); }")
endforeach()
foreach(expression
    "sizeof(part!(T))"
    "sizeof(part!(T) *)"
    "sizeof(T part!(*))"
    "sizeof(part!(T *))"
    "sizeof(T *part!(const))"
    "sizeof(T part!([2uptr]))"
    "sizeof(T [part!(2uptr)])"
    "sizeof(T [2uptr] part!([3uptr]))"
    "sizeof(T (*part!(const))[2uptr])"
    "sizeof(T (part!(*))[2uptr])"
    "sizeof(T (*)(part!(in T)))"
    "sizeof(T (*)(in part!(T)))"
    "sizeof(T (*)(in T part!(*)))"
    "sizeof(T (*)(in T part!(arg)))"
    "sizeof(T (*)(in T (part!(*arg))))"
    "sizeof(T (*)(in T arg[part!(2uptr)]))"
    "sizeof((part!(T) *)0uptr)"
    "sizeof((T part!(*))0uptr)"
    "sizeof((T (*)(part!(in T)))0uptr)"
    "sizeof(T (*)(in T) [[part!(abi(\"cross\"))]])"
    "sizeof(T (*)(in T) part!([[abi(\"cross\")]]))")
    check("[[aligned(early(unknown!()) + 0uptr * ${expression})]] static T test(in T value)
[[generic(last!(T))]] { return value; }
$::static_assert(test(300u32) == 300u32, \"generic binding lost\");")
endforeach()
