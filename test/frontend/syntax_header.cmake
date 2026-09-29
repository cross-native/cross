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

set(compose [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    $::static_assert($::meta::is_kind(header, "deferred"), "header was classified before its fragments");
    return $::quote { $::unquote(header) $::unquote($::syntax::capture(input, "body")) };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
]=])

foreach(header
        "static u32 test() [[generic(params!(u32 N))]]"
        "[[generic(params!(u32 N))]] static u32 test()"
        "static [[generic(params!(u32 N))]] u32 test()"
        "static u32 [[generic(params!(u32 N))]] test()"
        "static u32 test() [[params!(generic(u32 N))]]"
        "static u32 test<params!(u32 N)>()")
    string(MD5 case "${header}")
    check(captured_value_header_${case} "${compose}
compose ${header} { return N; }
$::static_assert(test<7u32>() == 7u32, \"value header generic was lost\");" 0)
endforeach()

foreach(header
        "static T test(in T value) [[generic(params!(T))]]"
        "[[generic(params!(T))]] static T test(in T value)"
        "static T test<params!(T)>(in T value)"
        "static T test(in T value) [[params!(generic(T))]]")
    string(MD5 case "${header}")
    check(captured_type_header_${case} "${compose}
compose ${header} { return value; }
$::static_assert(test(7u32) == 7u32, \"type header generic was lost\");" 0)
endforeach()

check(captured_shadowed_alias "${compose}
typedef u32 T();
compose static T test(in T value) [[generic(params!(T))]] { return value; }
$::static_assert(test(9u32) == 9u32, \"late generic did not shadow the callable alias\");" 0)

check(captured_angle_value_alias "${compose}
typedef u32 Count;
compose static u32 test<Count N, params!(T)>() { return N; }
$::static_assert(test<7u32, u32>() == 7u32, \"pending angle header rejected a scalar alias\");" 0)

check(captured_tag_types "${compose}
struct Pair { u32 value; };
enum Code { code = 4 };
compose static struct Pair record(in u32 value) [[generic(params!(u32 N))]] {
    struct Pair result = { value + N }; return result;
}
compose static enum Code enumeration() [[generic(params!(u32 N))]] { return code; }
$::static_assert(record<3u32>(4u32).value == 7u32, \"record header classification failed\");
$::static_assert((u32)enumeration<3u32>() == 4u32, \"enum header classification failed\");" 0)

check(captured_qualified_alias "${compose}
namespace Types { typedef u32 Value; }
compose static Types::Value test(in Types::Value value) [[generic(params!(u32 N))]] {
    return value + N;
}
$::static_assert(test<3u32>(4u32) == 7u32, \"qualified header alias was deferred as an unknown generic\");" 0)

check(independent_capture_with_pending_generics [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
struct Tag { u32 value; };
namespace Types { typedef u32 Value; }
[[syntax_expander]] static $::meta::tokens known(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::static_assert(!$::meta::is_kind(value, "deferred"), "independent lookup was deferred");
    return $::quote { $::unquote(value) };
}
syntax Known : expression { prefix "known"; match "(" value:expr ")"; expand known; }
syntax Known;
static u32 first() [[aligned(known(sizeof(struct Tag))), generic(params!(T))]] { return 1u32; }
static u32 second() [[aligned(known(sizeof(Types::Value))), generic(params!(T))]] { return 2u32; }
$::static_assert(first<u32>() + second<u32>() == 3u32, "independent capture changed");
]=] 0)

set(discard_header [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    $::static_assert($::meta::is_kind($::syntax::node(input, "header"), "deferred"),
                    "opaque header did not defer");
    return $::quote {};
}
syntax Discard : item { prefix "discard"; match header:function_header body:block; expand discard; }
syntax Discard;
]=])
foreach(header
        "static u32 discarded() [[generic(unknown!(u32 N))]]"
        "static T discarded(in T value) [[generic(unknown!(T))]]"
        "static T discarded<unknown!(T)>(in T value)"
        "[[unknown!(noinline)]] static u32 discarded()"
        "static u32 discarded() [[abi(unknown!(\"not-an-abi\"))]]"
        "static u32 (*discarded(in u32 value))(unknown!()) [[generic(unknown!(T))]]")
    string(MD5 case "${header}")
    check(discarded_header_${case} "${discard_header}
discard ${header} { completely noncore body; unknown!{discarded}; }" 0)
endforeach()

foreach(header
        "static u32 (*not_function)(unknown!()) [[generic(unknown!(T))]]"
        "static T not_function [[generic(unknown!(T))]]"
        "static u32 unknown!(not_function) [[generic(unknown!(T))]]"
        "[[, generic(unknown!(T))]] static u32 not_function()"
        "static u32 not_function(unknown!()) [[generic()]]"
        "static T not_function<unknown!(T)(in T value)"
        "static absent::T not_function() [[generic(unknown!(T))]]")
    string(MD5 case "${header}")
    check(rejected_header_${case} "${discard_header}
discard ${header} { unknown!{discarded}; }" 1 "error: syntax-match error for active prefix")
endforeach()

foreach(category function_decl function_def)
    set(suffix ";")
    set(after "global T kept(in T value) [[generic(T)]] { return value; }")
    if(category STREQUAL "function_def")
        set(suffix "{ return value; }")
        set(after "")
    endif()
    check(captured_${category} "
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax unit = $::syntax::node(input, \"unit\");
    $::static_assert($::meta::is_kind(unit, \"deferred\"), \"function unit was classified early\");
    return $::quote { $::unquote(unit) };
}
syntax Copy : item { prefix \"copy\"; match unit:${category}; expand copy; }
syntax Copy;
copy global T kept(in T value) [[generic(params!(T))]] ${suffix}
${after}
$::static_assert(kept(11u32) == 11u32, \"function category lost its generic\");" 0)
endforeach()

foreach(category function function_raw)
    check(raw_${category}_discard "
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax Drop : item { prefix \"drop\"; match body:${category}; expand drop; }
syntax Drop;
drop static T ignored(in T value) [[generic(unknown!(T))]] { not core syntax; unknown!{}; }
drop static T ignored_angle<unknown!(T)>(in T value) { not core syntax; unknown!{}; }" 0)
    check(raw_${category}_copy "
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::syntax::capture(input, \"body\");
}
syntax Copy : item { prefix \"copy\"; match body:${category}; expand copy; }
syntax Copy;
copy static T kept(in T value) [[generic(params!(T))]] { return value; }
$::static_assert(kept(13u32) == 13u32, \"raw function generic was lost\");" 0)
endforeach()

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

check(captured_header_owner_order "${prefix}
[[syntax_expander]] static $::meta::tokens owner(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"header-capture-owner\");
    return $::quote { $::unquote($::syntax::node(input, \"header\"))
                     $::unquote($::syntax::capture(input, \"body\")) };
}
syntax Owner : item { prefix \"owner\"; match header:function_header body:block; expand owner; }
syntax Owner;
owner static u32 test() [[aligned(notice(unknown!{discarded})), generic(bad!(T))]] { return 0u32; }"
    1 "note: header-capture-owner" "note: earlier-header-owner" "error: division by zero")

check(captured_header_parse_roundtrip [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    $::meta::syntax decorated = $::meta::parse("function_header",
        $::quote { [[params!(noinline)]] $::unquote(header) }, $::syntax::context(input));
    $::static_assert($::meta::is_kind(decorated, "deferred"), "decorated header was not deferred");
    $::meta::syntax definition = $::meta::parse("function_def", $::quote {
        $::unquote(decorated) $::unquote($::syntax::capture(input, "body"))
    }, $::syntax::context(input));
    return $::quote { $::unquote(definition) };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
compose static T test(in T value) [[generic(params!(T))]] { return value; }
$::static_assert(test(17u32) == 17u32, "header parse roundtrip lost its generic");
]=] 0)

check(decorated_core_header [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    $::static_assert(!$::meta::is_kind(header, "deferred"), "original header was not settled");
    header = $::meta::parse("function_header", $::quote {
        [[params!(noinline)]] $::unquote(header) [[generic(params!(u32 N))]]
    }, $::syntax::context(input));
    $::static_assert($::meta::is_kind(header, "deferred"), "opaque decoration was not deferred");
    return $::quote { $::unquote(header) $::unquote($::syntax::capture(input, "body")) };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
compose static u32 kept(in u32 value) { return value; }
$::static_assert(kept<3u32>(19u32) == 19u32, "decorated core header lost its generic");
]=] 0)

check(header_before_body_fragment "${prefix}
[[macro]] static $::meta::tokens body(in $::meta::tokens input) {
    u32 value = 1u32 / 0u32;
    return $::quote { { return 0u32; } };
}
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, \"header\")) body!() };
}
syntax Compose : item { prefix \"compose\"; match header:function_header \";\"; expand compose; }
syntax Compose;
compose static u32 kept() [[aligned(notice(unknown!{discarded})), generic(params!(T))]];"
    1 "note: earlier-header-owner" "error: division by zero")

check(header_body_fragment_context [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens body(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    return $::quote {
        $::unquote($::syntax::node(input, "header"))
        body!($::unquote($::syntax::capture(input, "body")))
    };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
compose static T kept(in T value) [[generic(params!(T))]] { T copy = value; return copy; }
$::static_assert(kept(23u32) == 23u32, "body fragment lost header generic bindings");
]=] 0)

foreach(tag struct union)
    foreach(generic "T" "params!(T)")
        string(MD5 case "${tag}-${generic}")
        check(inline_tag_generic_${case} "
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
static ${tag} Result { u32 value; } make(in T value) [[generic(${generic})]] {
    ${tag} Result result = { (u32)value }; return result;
}
$::static_assert(make(29u32).value == 29u32, \"inline result tag hid header generics\");" 0)
    endforeach()
endforeach()

check(inline_enum_generic [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
static enum Result { value = 31 } make(in T input) [[generic(params!(T))]] { return value; }
$::static_assert((u32)make(1u32) == 31u32, "inline enum hid header generics");
]=] 0)

foreach(category function function_raw)
    check(raw_inline_tag_${category} "
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax Drop : item { prefix \"drop\"; match value:${category}; expand drop; }
syntax Drop;
drop static struct Record { u32 field; } make(in u32 value) { not core syntax; unknown!{}; }
drop static union Union { u32 field; u64 other; } make_union() { not core syntax; unknown!{}; }
drop static enum Code { code = 1 } make_enum() { not core syntax; unknown!{}; }" 0)
endforeach()

check(captured_inline_tag "${compose}
compose static struct Result { u32 value; } make(in T value) [[generic(params!(T))]] {
    struct Result result = { (u32)value }; return result;
}
$::static_assert(make(37u32).value == 37u32, \"captured inline result tag lost its binding\");" 0)

check(inline_tag_owner_order "${prefix}
static struct Result { u32 values[notice(unknown!{discarded})]; } make(in T value)
    [[generic(bad!(T))]] { struct Result result = {}; return result; }"
    1 "note: earlier-header-owner" "error: division by zero")

foreach(tag "struct Result { unknown!(members) }" "union Result { unknown!(members) }"
            "enum Result { unknown!(enumerators) }")
    string(MD5 case "${tag}")
    check(opaque_inline_tag_${case} "${discard_header}
discard static ${tag} ignored() { noncore body; unknown!(); }" 0)
endforeach()

check(raw_choice_failed_header [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
    $::static_assert($::syntax::is_variant($::syntax::at(input, "branch", 0uptr), "raw"),
                    "a nonfunction was matched as a raw function");
    return $::quote {};
}
syntax Drop : item {
    prefix "drop";
    match branch:choice(function:(value:function_raw) | raw:("static" "u32" "object" body:block));
    expand drop;
}
syntax Drop;
drop static u32 object { not a function body; unknown!(); }
]=] 0)

check(nested_inline_tag_generic [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[noinline]] static struct Outer { struct Inner { u16 value; } nested; }
make(in T value) [[generic(params!(T))]] {
    struct Outer result = { { (u16)value } }; return result;
}
$::static_assert(make(41u32).nested.value == 41u16, "nested inline tags lost header context");
]=] 0)

check(captured_inline_tag_members "${compose}
compose [[noinline]] static struct Result { params!(u32 value;) } make(in u32 value) {
    struct Result result = { value }; return result;
}
$::static_assert(make(43u32).value == 43u32, \"deferred tag members were not reparsed\");" 0)

check(captured_inline_enum_members "${compose}
compose [[noinline]] static enum Result [[underlying(u16)]] { params!(value = 47u16) }
make() { return value; }
$::static_assert((u16)make() == 47u16, \"deferred enumerators were not reparsed\");" 0)

check(inline_tag_attribute_owner_order "${prefix}
static struct Result [[aligned(notice(unknown!{discarded}))]] { u32 value; }
make(in T value) [[generic(bad!(T))]] { struct Result result = { 0u32 }; return result; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(inline_enum_owner_order "${prefix}
static enum Result { first = notice(unknown!{discarded}) }
make(in T value) [[generic(bad!(T))]] { return first; }"
    1 "note: earlier-header-owner" "error: division by zero")

foreach(tag "struct Result { u32 first u32 second; unknown!(); }"
            "enum Result { first = , unknown!() }")
    string(MD5 case "${tag}")
    check(invalid_before_opaque_tag_${case} "${discard_header}
discard static ${tag} ignored() { noncore body; unknown!(); }" 1
        "error: syntax-match error for active prefix")
endforeach()

check(grouped_pointer_fragment [=[
[[macro]] static $::meta::tokens part(in $::meta::tokens input) { return input; }
[[noinline]] static u32 target(in u32 value) { return value + 3u32; }
global u32 entry() {
    u32 (part!(*callback))(in u32 value) = &target;
    return callback(5u32);
}
]=] 0)

check(grouped_names [=[
typedef u32 Word;
[[noinline]] static u32 (identity)(in u32 (value)) { return value; }
[[noinline]] static u32 ((nested))(in u32 value) { return value + 1u32; }
global u32 entry() {
    u32 (Word) = 7u32;
    return identity(Word) + nested(Word);
}
$::static_assert(identity(5u32) == 5u32 && nested(5u32) == 6u32, "grouped names changed binding");
]=] 0)

check(grouped_header_fragments "${prefix}
[[macro]] static $::meta::tokens named(in $::meta::tokens input) { return input; }
static T (named!(identity))(in T value)
    [[aligned(notice(unknown!{discarded})), generic(params!(T))]] { return value; }
$::static_assert(identity(53u32) == 53u32, \"grouped generated header lost generics\");"
    0 "note: earlier-header-owner")

foreach(name "identity<T>" "identity<part!(T)>" "part!(identity<T>)"
             "(identity<part!(T)>)" "part!((identity<part!(T)>))")
    string(MD5 case "${name}")
    check(grouped_angle_header_${case} "
[[macro]] static $::meta::tokens part(in $::meta::tokens input) { return input; }
typedef u8 T;
static T (${name})(in T value) { return value; }
$::static_assert(identity(300u32) == 300u32, \"grouped angle generics did not cover the result type\");" 0)
endforeach()

foreach(name "(identity<params!(T)>)" "((params!(identity<T>)))")
    string(MD5 case "${name}")
    check(grouped_header_order_${case} "${prefix}
typedef u8 T;
[[aligned(notice(unknown!{discarded}))]] static T ${name}(in T (value)) {
    return value;
}
$::static_assert(identity(300u32) == 300u32, \"grouped header lost generic scope\");"
        0 "note: earlier-header-owner")
endforeach()

check(grouped_late_failure_order "${prefix}
[[aligned(notice(unknown!{discarded}))]] static T ((test<bad!(T)>))(in T value) {
    return value;
}" 1 "note: earlier-header-owner" "error: division by zero")

foreach(expression
        "sizeof(T (params!(*))(in u32))"
        "sizeof(T params!(*))"
        "sizeof(T (*)(params!(in u32)))"
        "sizeof(T [params!(2uptr)])"
        "$::alignof(T (params!(*))(in u32))"
        "sizeof((T (params!(*))(in u32))0uptr)"
        "alignment::<T (params!(*))(in u32)>()")
    string(MD5 case "${expression}")
    check(opaque_header_type_order_${case} "${prefix}
static u32 alignment<U>() { return 16u32; }
static T test(in T value)
    [[aligned(${expression} + notice(unknown!{discarded})), generic(bad!(T))]] {
    return value;
}" 1 "note: earlier-header-owner" "error: division by zero")
    check(opaque_header_type_valid_${case} "${prefix}
static u32 alignment<U>() { return 16u32; }
static T test(in T value)
    [[aligned((${expression} != 0uptr ? 16u32 : 16u32) +
               notice(unknown!{discarded}) - 16u32), generic(params!(T))]] {
    return value;
}
$::static_assert(test(300u32) == 300u32, \"opaque type damaged final generic bindings\");"
        0 "note: earlier-header-owner")
endforeach()

foreach(expression "sizeof(T [notice(unknown!{discarded})])"
                   "sizeof(T (*)(in u32 values[notice(unknown!{discarded})]))")
    string(MD5 case "${expression}")
    check(opaque_type_owner_order_${case} "${prefix}
static T test(in T value) [[aligned(${expression}), generic(bad!(T))]] { return value; }"
        1 "note: earlier-header-owner" "error: division by zero")
endforeach()

check(opaque_header_value_order "${prefix}
static u32 test() [[aligned(sizeof(N params!(+ 1u32)) + notice(unknown!{discarded})),
                   generic(bad!(u32 N))]] { return N; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(opaque_header_value_valid "${prefix}
static u32 test() [[aligned(sizeof(N params!(+ 1u32)) + notice(unknown!{discarded}) - sizeof(u32)),
                   generic(params!(u32 N))]] { return N; }
$::static_assert(test<300u32>() == 300u32, \"value generic was forced into type grammar\");"
    0 "note: earlier-header-owner")

foreach(expression "sizeof(N * notice(unknown!{discarded}))"
                   "sizeof(Target(notice(unknown!{discarded})))")
    string(MD5 case "${expression}")
    check(opaque_header_expression_owner_${case} "${prefix}
static u32 Target(in u32 value) { return value; }
static u32 test() [[aligned(${expression} + 16u32 - sizeof(u32)),
                   generic(params!(u32 N))]] { return N; }
$::static_assert(test<300u32>() == 300u32, \"type probing damaged an expression owner\");"
        0 "note: earlier-header-owner")
endforeach()

check(opaque_probe_split_token_position "${prefix}
static u32 alignment<U>() { return 16u32; }
static T test(in T value)
    [[aligned(sizeof(T [alignment::<alignment::<u32>>() + notice(unknown!{discarded})])),
      generic(bad!(T))]] { return value; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(generic_function_alignment [=[
static u32 alignment<u32 N>() { return N; }
[[aligned(alignment<16u32>())]] static u32 first() { return 1u32; }
[[aligned(alignment<N>())]] static T second<T, u32 N>(in T value) { return value; }
$::static_assert(first() == 1u32 && second<u32, 32u32>(300u32) == 300u32,
                "generic function alignment did not instantiate");
]=] 0)

foreach(wrapper "" "compose ")
    string(MD5 case "${wrapper}")
    check(known_generic_type_slot_${case} "${compose}
typedef u32 Param;
[[syntax_expander]] static $::meta::tokens wrong(in $::meta::syntax_match input) {
    $::syntax::error($::syntax::span(input), \"required parameter type dispatched expression syntax\");
    return $::quote { 16u32 };
}
syntax ParameterOwner : expression { prefix \"Param\"; match input:paren; expand wrong; }
syntax ParameterOwner;
namespace Helpers {
    static u32 alignment<u32 N, U>() { return N; }
}
${wrapper}static T test(in T value)
    [[aligned(Helpers::alignment::<16u32, T(Param(params!(in u32)))>()),
      generic(params!(T))]] { return value; }
$::static_assert(test(300u32) == 300u32, \"known type slot lost nested callable grammar\");" 0)
endforeach()

check(known_generic_type_slot_prefix_priority "${prefix}
static u32 alignment<U>() { return 16u32; }
static T test(in T value)
    [[aligned(alignment::<notice(unknown!{discarded})>()), generic(bad!(T))]] { return value; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(grouped_macro_input_opaque "${prefix}
[[macro]] static $::meta::tokens choose(in $::meta::tokens ignored) {
    return $::quote { (function) };
}
[[aligned(notice(unknown!{discarded}))]] static u32
(choose!(unknown!{discarded}))(in u32 value) { return value; }
$::static_assert(function(300u32) == 300u32, \"grouped name fragment failed\");"
    0 "note: earlier-header-owner")

check(captured_grouped_generics "${compose}
typedef u8 T;
compose [[noinline]] static T ((identity<params!(T)>))(in T (value)) { return value; }
$::static_assert(identity(300u32) == 300u32, \"captured grouped header lost generic scope\");" 0)

foreach(header "static T ((ignored<unknown!(T)>))(in T value)"
               "static u32 (ignored)(unknown!())")
    string(MD5 case "${header}")
    check(discard_grouped_header_${case} "${discard_header}
discard ${header} { not core syntax; unknown!(); }" 0)
endforeach()

foreach(header "static u32 (unknown!(name))()"
               "static u32 (*object)(unknown!())")
    string(MD5 case "${header}")
    check(reject_unproved_grouped_header_${case} "${discard_header}
discard ${header} { unknown!(); }" 1 "error: syntax-match error for active prefix")
endforeach()
