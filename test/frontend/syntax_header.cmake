# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(check case source expected_status)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL expected_status)
        message(FATAL_ERROR "${case}: unexpected result ${status}\n${out}\n${err}")
    endif()
    set(previous -1)
    foreach(marker IN LISTS ARGN)
        string(FIND "${err}" "${marker}" position)
        string(REGEX MATCHALL "${marker}" occurrences "${err}")
        list(LENGTH occurrences count)
        if(NOT count EQUAL 1 OR NOT position GREATER previous)
            message(FATAL_ERROR "${case}: ${marker} missing, repeated or out of order\n${err}")
        endif()
        set(previous ${position})
    endforeach()
    if(err MATCHES "procedural macro is not visible")
        message(FATAL_ERROR "${case}: discarded input was executed\n${err}")
    endif()
endfunction()

set(prefix [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens bad(in $::meta::tokens input) {
    u32 value = 1u32 / 0u32;
    return input;
}
[[syntax_expander]] static $::meta::tokens notice(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), "earlier-header-owner");
    return $::quote { 16u32 };
}
syntax Notice : expression { prefix "notice"; match input:paren; expand notice; }
syntax Notice;
]=])

foreach(expression "notice(unknown!{discarded})" "(T)notice(unknown!{discarded})"
                   "sizeof(T *) + notice(unknown!{discarded})")
    string(MD5 case "${expression}")
    check(order_${case} "${prefix}
static u32 test() [[aligned(${expression}), generic(bad!(T))]] { return 0u32; }"
        1 "note: earlier-header-owner" "error: division by zero")
endforeach()

check(parameter_before_generic "${prefix}
static u32 test(in u32 values[notice(unknown!{discarded})])
    [[generic(bad!(T))]] { return 0u32; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(parameter_fragment_before_generic "${prefix}
[[macro]] static $::meta::tokens parameter(in $::meta::tokens input) {
    return $::quote { in u32 value[notice(unknown!{discarded})] };
}
static u32 test(parameter!()) [[generic(bad!(T))]] { return 0u32; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(nested_output "${prefix}
[[syntax_expander]] static $::meta::tokens outer(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"outer-header-owner\");
    return $::quote { notice(unknown!{discarded}) };
}
syntax Outer : expression { prefix \"outer\"; match input:paren; expand outer; }
syntax Outer;
static u32 test() [[aligned(outer()), generic(bad!(T))]] { return 0u32; }"
    1 "note: outer-header-owner" "note: earlier-header-owner" "error: division by zero")

check(nested_macro_before_later_fragment "${prefix}
[[syntax_expander]] static $::meta::tokens outer(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"outer-header-owner\");
    return $::quote { params!(notice(unknown!{discarded})) };
}
syntax Outer : expression { prefix \"outer\"; match input:paren; expand outer; }
syntax Outer;
static u32 test() [[aligned(outer()), generic(bad!(T))]] { return 0u32; }"
    1 "note: outer-header-owner" "note: earlier-header-owner" "error: division by zero")

set(capture [=[
[[syntax_expander]] static $::meta::tokens capture(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::static_assert($::meta::is_kind(value, "deferred"), "pending capture was classified");
    return $::quote { $::unquote(value) };
}
syntax Capture : expression { prefix "capture"; match "(" value:expr ")"; expand capture; }
syntax Capture;
]=])
check(deferred_names "${prefix}${capture}
static T first(in T value) [[aligned(capture(sizeof(T *))), generic(params!(T))]] { return value; }
static T second(in T value) [[aligned(capture((T)16u32)), generic(params!(T))]] { return value; }
static u32 third() [[aligned(capture(N)), generic(params!(u32 N))]] { return 3u32; }
global u32 entry() { return first(1u32) + second(2u32) + third<16u32>(); }" 0)

check(known_capture_without_fragments "${prefix}
typedef u32 T;
[[syntax_expander]] static $::meta::tokens known(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, \"value\");
    $::static_assert(!$::meta::is_kind(value, \"deferred\"), \"settled capture was deferred\");
    return $::quote { $::unquote(value) };
}
syntax Known : expression { prefix \"known\"; match \"(\" value:expr \")\"; expand known; }
syntax Known;
static u32 test() [[aligned(known(sizeof(T)))]] { return 0u32; }
global u32 entry() { return test(); }" 0)

check(bounded_precedence "${prefix}
[[syntax_expander]] static $::meta::tokens sum(in $::meta::syntax_match input) {
    return $::quote { 2u32 + 2u32 };
}
syntax Sum : expression { prefix \"sum\"; match input:paren; expand sum; }
syntax Sum;
static T test(in T value) [[aligned(2u32 * sum()), generic(params!(T))]] { return value; }
global u32 entry() { return test(3u32); }" 0)

check(bounded_trailing_tokens "${prefix}
[[syntax_expander]] static $::meta::tokens extra(in $::meta::syntax_match input) {
    return $::quote { 16u32; };
}
syntax Extra : expression { prefix \"extra\"; match input:paren; expand extra; }
syntax Extra;
static u32 test() [[aligned(extra()), generic(params!(T))]] { return 0u32; }"
    1 "prepared expression must contain one assignment expression")

check(body_not_prepared "${prefix}
static T test(in T value) [[aligned(notice()), generic(params!(T))]] {
    return value + bad!();
}"
    1 "note: earlier-header-owner" "error: division by zero")

foreach(limit "-feval-depth-limit=2" "-feval-step-limit=64" "-feval-memory-limit=256")
    execute_process(COMMAND "${CC}" -S "${limit}" "${OUTPUT}/deferred_names.x"
        -o "${OUTPUT}/limited.s" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "budget exceeded" OR
       NOT err MATCHES ":[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "header ${limit} was not bounded\n${out}\n${err}")
    endif()
endforeach()
