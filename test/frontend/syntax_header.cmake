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

# A later declarator has its own generic-name classifier, including grouped
# names and trailing attributes. These cases deliberately use a fixed result
# type; independent interpretation of shared result specifiers is separate.
check(declarator_generic_classifiers [=[
typedef u8 T;
typedef u16 U;
global u32 first<T>(in T value), second(in T value), third<U>(in U value);
global u32 first<T>(in T value) { return (u32)value; }
global u32 second(in T value) { return (u32)value; }
global u32 third<U>(in U value) { return (u32)value; }
global u32 plain(in T value), ((grouped<T>))(in T value), after(in T value);
global u32 plain(in T value) { return (u32)value; }
global u32 grouped<T>(in T value) { return (u32)value; }
global u32 after(in T value) { return (u32)value; }
global u32 attribute<T>(in T value) [[noinline]], after_attribute(in T value);
global u32 attribute<T>(in T value) { return (u32)value; }
global u32 after_attribute(in T value) { return (u32)value; }
global u32 shared_first<T>(in T value), shared_second<T>(in T value);
global u32 shared_first<T>(in T value) { return (u32)value; }
global u32 shared_second<T>(in T value) { return (u32)value; }
global u32 mixed<T>(in T value), (*callback)(in T value) = second;
global u32 mixed<T>(in T value) { return (u32)value; }
$::static_assert(first(300u32) + second(40u8) + third(500u32) == 840u32,
    "generic classifier leaked between declarators");
$::static_assert(plain(1u8) + grouped(300u32) + after(2u8) == 303u32,
    "grouped later declarator lost its generic scope");
$::static_assert(attribute(300u32) + after_attribute(3u8) == 303u32,
    "generic list leaked past a trailing attribute");
$::static_assert(shared_first(300u32) + shared_second(400u64) == 700u32,
    "sibling generic lists did not apply independently");
]=] 0)

check(header_parameter_type_lookup [=[
typedef u8 input;
global u32 sized<T>(in T input, in u8 (*values)[sizeof(input)]);
global u32 sized<U>(in U source, in u8 (*storage)[sizeof(source)]) { return (u32)sizeof(*storage); }
global u32 plain(in u32 input, in u8 (*values)[sizeof(input)]);
global u32 plain(in u32 source, in u8 (*storage)[sizeof(source)]) { return (u32)sizeof(*storage); }
global u32 nested<T>(in u32 (*callback)(in T input, in u8 (*values)[sizeof(input)]));
global u32 nested<U>(in u32 (*callback)(in U source, in u8 (*storage)[sizeof(source)])) { return 4u32; }
global u32 callback(in u32 source, in u8 (*storage)[sizeof(source)]) { return (u32)sizeof(*storage); }
static u8 (*result<T>(in T input, in u8 (*values)[sizeof(input)]))[sizeof(input)] { return values; }
static u32 vector<T>(in T input, in u8 [[vector_size(sizeof(input))]] *value) { return (u32)sizeof(*value); }
$::static_assert(sized(17u32, (void *)0uptr) == 4u32 && sized(3u16, (void *)0uptr) == 2u32,
    "parameter type scope or alias shadowing failed");
$::static_assert(plain(17u32, (void *)0uptr) == 4u32 && nested(callback) == 4u32,
    "ordinary or nested prototype type scope failed");
$::static_assert(sizeof(*result(17u32, (void *)0uptr)) == 4uptr &&
    vector(17u32, (void *)0uptr) == 4u32, "result or vector bound lost its prototype cells");
]=] 0)

check(grouped_function_suffix_attributes [=[
typedef u8 T;
[[macro]] static $::meta::tokens names(in $::meta::tokens input) { return input; }
static T identity<T>(in T value) { return value; }
static T (*factory<names!(T)>(in T value) [[noinline]])(in T argument) {
    return &identity<T>;
}
static u32 check() {
    u32 (*callback)(in u32 argument) = factory(0u32);
    return sizeof(callback(300u32)) == 4uptr;
}
$::static_assert(check() == 1u32, "grouped function suffix lost its generic scope");
]=] 0)

check(grouped_returned_callback_attribute_rejected [=[
static u32 (*factory(in u32 value))(in u32 argument) [[noinline]];
]=] 1 "error: function-only attribute cannot qualify a nested callable type")

foreach(category declaration function_decl function_def)
    set(suffix ";")
    set(after "static u32 copied<U>(in U source, in u8 (*storage)[sizeof(source)]) { return (u32)sizeof(*storage); }")
    if(category STREQUAL function_def)
        set(suffix "{ return (u32)sizeof(*values); }")
        set(after "")
    endif()
    check(header_parameter_capture_${category} "
[[macro]] static $::meta::tokens names(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, \"decl\")) };
}
syntax Keep : item { prefix \"keep\"; match decl:${category}; expand keep; }
syntax Keep;
[[syntax_expander]] static $::meta::tokens bound(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, \"value\")) };
}
syntax Bound : expression { prefix \"bound\"; match \"(\" value:expr \")\"; expand bound; }
syntax Bound;
keep static u32 copied<names!(T)>(in T input, in u8 (*values)[bound(sizeof(input))])
    ${suffix}
${after}
$::static_assert(copied(17u32, (void *)0uptr) == 4u32 && copied(3u16, (void *)0uptr) == 2u32,
    \"captured header lost earlier parameter type lookup\");" 0)
endforeach()

check(header_parameter_grouped_result_capture [=[
[[macro]] static $::meta::tokens names(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "decl")) };
}
syntax Keep : item { prefix "keep"; match decl:function_def; expand keep; }
syntax Keep;
keep static u8 (*result<names!(T)>(in T input, in u8 (*values)[sizeof(input)]))[sizeof(input)]
    { return values; }
$::static_assert(sizeof(*result(17u32, (void *)0uptr)) == 4uptr,
    "deferred result suffix lost its original prototype scope");
]=] 0)

check(header_parameter_gensym [=[
[[macro]] static $::meta::tokens build(in $::meta::tokens ignored) {
    $::meta::tokens name = $::meta::call_site($::quote { generated });
    $::meta::tokens cell = $::meta::gensym("input");
    return $::quote { static u32 $::unquote(name)<T>(in T $::unquote(cell),
        in u8 (*values)[sizeof($::unquote(cell))]) { return (u32)sizeof(*values); } };
}
build!()
$::static_assert(generated(17u32, (void *)0uptr) == 4u32,
    "generated prototype cell did not retain its fresh identity");
]=] 0)

check(header_parameter_value_is_not_constant [=[
static u32 invalid(in u32 input, in u8 (*value)[input]) { return 0u32; }
]=] 1 "error: runtime local or parameter is not a translation-time value")
check(header_parameter_forward_name [=[
static u32 invalid(in u8 (*value)[sizeof(input)], in u32 input) { return 0u32; }
]=] 1 "error: unresolved name 'input'")
check(header_parameter_nested_scope_does_not_leak [=[
static u32 invalid(in u32 (*callback)(in u32 input), in u8 (*value)[sizeof(input)]) { return 0u32; }
]=] 1 "error: unresolved name 'input'")
check(header_parameter_sibling_scope_does_not_leak [=[
global u32 first(in u32 input), second(in u8 (*values)[sizeof(input)]);
]=] 1 "error: unresolved name 'input'")

check(generic_dependent_extent_interfaces [=[
global u32 extent<T>(in u8 (*values)[sizeof(T)]);
global u32 extent<U>(in u8 (*storage)[sizeof(U)]) { return (u32)sizeof(*storage); }
global u32 formula<uptr N>(in u8 (*values)[N + N]);
global u32 formula<uptr M>(in u8 (*storage)[2uptr * M]) { return (u32)sizeof(*storage); }
global u32 nested<T>(in u32 (*callback)(in u8 (*values)[sizeof(T)]));
global u32 nested<U>(in u32 (*callback)(in u8 (*storage)[sizeof(U)])) { return 4u32; }
global u32 callback(in u8 (*values)[4uptr]) { return (u32)sizeof(*values); }
global u32 vector_extent<uptr N>(in u8 [[vector_size(N)]] *value);
global u32 vector_extent<uptr M>(in u8 [[vector_size(M)]] *storage) { return (u32)sizeof(*storage); }
global u32 vector_units<uptr N>(in u32 [[ext_vector_type(N)]] *value);
global u32 vector_units<uptr M>(in u32 [[vector_size(M * sizeof(u32))]] *storage) { return (u32)sizeof(*storage); }
global u32 known<T>(in u8 (*value)[sizeof(T)]);
global u32 known<U>(in u8 (*storage)[4uptr]) { return (u32)sizeof(*storage); }
static u8 object[4uptr];
global u32 value_type<uptr N, u8 (*)[N + N] P>();
global u32 value_type<uptr M, u8 (*)[2uptr * M] Q>() { return (u32)sizeof(*Q); }
global u8 (*result_type<uptr N>(in u8 (*values)[N + N]))[N + N];
global u8 (*result_type<uptr M>(in u8 (*storage)[2uptr * M]))[2uptr * M] { return storage; }
static u32 check() {
    u8 wide[4uptr]; u8 small[2uptr];
    return extent<u32>(&wide) + extent<u16>(&small) + formula<2uptr>(&wide) + formula<1uptr>(&small);
}
$::static_assert(check() == 12u32, "dependent interfaces did not match");
$::static_assert(nested<u32>(callback) == 4u32, "nested callback extent failed");
$::static_assert(vector_extent<4uptr>((void *)0uptr) == 4u32, "vector byte extent failed");
$::static_assert(vector_units<4uptr>((void *)0uptr) == 16u32, "vector lane/byte units did not agree");
$::static_assert(known<u32>((void *)0uptr) == 4u32, "dependent/fixed extents did not agree");
$::static_assert(value_type<2uptr, &object>() == 4u32, "value parameter type extents did not agree");
$::static_assert(sizeof(*result_type<2uptr>(&object)) == 4uptr, "result extents did not agree");
]=] 0)

check(generic_unused_dependent_extents [=[
global u32 unused<uptr N>(in u8 (*values)[N + 1uptr]);
global u32 unused<uptr M>(in u8 (*storage)[M + 2uptr]) { return 0u32; }
]=] 0)

foreach(change "const u8 (*values)[N]" "u16 (*values)[N]")
    string(MD5 case "${change}")
    check(generic_known_shape_mismatch_${case} "
global u32 mismatch<uptr N>(in u8 (*values)[N]);
global u32 mismatch<uptr N>(in ${change}) { return 0u32; }"
        1 "error: generic declarations")
endforeach()

check(generic_invalid_constant_extent [=[
global u32 invalid<uptr N>(in u8 (*values)[N]);
global u32 invalid<uptr N>(in u8 (*values)[0uptr]) { return 0u32; }
]=] 1 "error: fixed array bound must be a positive integer")

check(generic_incomplete_extent [=[
global u32 invalid<uptr N>(in u8 (*values)[N]);
global u32 invalid<uptr N>(in u8 (*values)[]) { return 0u32; }
]=] 1 "error: an array parameter requires a positive fixed bound")

check(generic_known_extent_mismatch [=[
global u32 mismatch<T>(in u8 (*values)[4uptr]);
global u32 mismatch<U>(in u8 (*values)[8uptr]) { return 0u32; }
]=] 1 "error: generic declarations")

foreach(change "" "-> \"private-result-endpoint\"")
    string(MD5 case "${change}")
    if(change STREQUAL "")
        set(parameter "out u8 (*values)[N]")
    else()
        set(parameter "in u8 (*values)[N]")
    endif()
    check(generic_callable_mismatch_${case} "
global u32 mismatch<uptr N>(in u8 (*values)[N]);
global u32 mismatch<uptr N>(${parameter}) ${change} { return 0u32; }"
        1 "error: generic declarations")
endforeach()

foreach(instance 2 3)
    if(instance EQUAL 2)
        set(expected 0)
        set(marker "")
    else()
        set(expected 1)
        set(marker "error: generic declarations of 'sometimes' have incompatible interfaces after substitution")
    endif()
    check(generic_extent_per_instance_${instance} "
global u32 sometimes<uptr N>(in u8 (*values)[N * N]);
global u32 sometimes<uptr M>(in u8 (*storage)[2uptr * M]) { return (u32)sizeof(*storage); }
static u32 check() {
    u8 data[${instance}uptr * 2uptr]; return sometimes<${instance}uptr>(&data);
}
" ${expected} ${marker})
endforeach()

check(generic_extent_rechecked_after_valid_instance [=[
global u32 sometimes<uptr N>(in u8 (*values)[N * N]);
global u32 sometimes<uptr M>(in u8 (*storage)[2uptr * M]) { return (u32)sizeof(*storage); }
static u32 check() {
    u8 first[4uptr]; u8 second[6uptr];
    return sometimes<2uptr>(&first) + sometimes<3uptr>(&second);
}
]=] 1 "error: generic declarations of 'sometimes' have incompatible interfaces after substitution")

check(generic_multiple_extent_promises [=[
global u32 extent<uptr N>(in u8 (*values)[N + N]);
global u32 extent<uptr K>(in u8 (*values)[4uptr * (K / 2uptr)]);
global u32 extent<uptr M>(in u8 (*storage)[2uptr * M]) { return (u32)sizeof(*storage); }
static u32 check() { u8 data[6uptr]; return extent<3uptr>(&data); }
]=] 1 "error: generic declarations of 'extent' have incompatible interfaces after substitution")

check(generic_invocation_extent_interfaces [=[
static u32 size<T>(in $::meta::tokens input, in u8 (*values)[$::meta::len($::quote { a b }) * sizeof(T)]);
static u32 size<U>(in $::meta::tokens source, in u8 (*storage)[sizeof(U) * $::meta::len($::quote { a b })]) {
    return (u32)sizeof(*storage);
}
[[macro]] static $::meta::tokens verify(in $::meta::tokens input) {
    $::static_assert(size<u8>(input, (void *)0uptr) == 2u32, "stale invocation extent");
    $::static_assert(size<u32>(input, (void *)0uptr) == 8u32, "stale substituted extent");
    return $::quote { 16uptr };
}
[[aligned(verify!(a))]] static u32 one() { return 1u32; }
[[aligned(verify!(a b c d))]] static u32 four() { return 4u32; }
]=] 0)

foreach(invoke unused used)
    set(suffix "")
    set(expected 0)
    set(marker "")
    if(invoke STREQUAL used)
        set(suffix "global u32 entry() { return verify!(a); }")
        set(expected 1)
        set(marker "error: generic declarations of 'size' have incompatible interfaces after substitution")
    endif()
    check(generic_invocation_extent_mismatch_${invoke} "
static u32 size<T>(in $::meta::tokens input, in u8 (*values)[$::meta::len($::quote { a b }) * sizeof(T)]);
static u32 size<U>(in $::meta::tokens source, in u8 (*storage)[$::meta::len($::quote { a b c }) * sizeof(U)]) {
    return (u32)sizeof(*storage);
}
[[macro]] static $::meta::tokens verify(in $::meta::tokens input) {
    size<u8>(input, (void *)0uptr); return $::quote { 1u32 };
}
${suffix}
" ${expected} ${marker})
endforeach()

check(generic_expansion_known_shape_mismatch [=[
static u32 shape<T>(in $::meta::tokens input, in u8 (*values)[sizeof(T)]);
static u32 shape<U>(in $::meta::tokens source, in u16 (*storage)[sizeof(U)]) { return 0u32; }
[[macro]] static $::meta::tokens verify(in $::meta::tokens input) { return $::quote { 1u32 }; }
global u32 entry() { return verify!(a); }
]=] 1 "error: generic declarations of 'shape' have incompatible interfaces")

check(opaque_array_operands [=[
[[macro]] static $::meta::tokens extent(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens operand(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Bound : expression { prefix "bound"; match "(" value:expr ")"; expand operand; }
syntax Bound;
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Keep : item { prefix "keep"; match value:declaration; expand keep; }
syntax Keep;
keep static u8 from_syntax[bound(4uptr)];
keep static u8 from_macro[extent!(4uptr)];
[[syntax_expander]] static $::meta::tokens build(in $::meta::syntax_match input) {
    return $::quote { keep static u8 from_node[$::unquote($::syntax::node(input, "value"))]; };
}
syntax Build : item { prefix "build"; match "(" value:expr ")"; expand build; }
syntax Build;
build(4uptr)
$::static_assert(sizeof(from_syntax) == 4uptr && sizeof(from_macro) == 4uptr && sizeof(from_node) == 4uptr,
    "opaque operands were given dummy constant values");
]=] 0)

check(declarator_generic_type_does_not_escape [=[
global u32 first<T>(in T value), second(in T value);
]=] 1 "error: expected Cross type")

check(declarator_generic_tag_scope [=[
typedef u8 T;
struct Argument { u8 outer; };
global u32 first<T>(in struct Argument { T value; } *argument),
    second(in struct Argument *argument);
global u32 second(in struct Argument *argument) { return (u32)argument->outer; }
]=] 0)

check(declarator_shared_results [=[
typedef u8 T;
global T first<T>(in T value), second(in T value), third<T>(in T value);
global T first<T>(in T value) { return value; }
global T second(in T value) { return value; }
global T third<T>(in T value) { return value; }
global T plain(in T value), ((later<T>))(in T value), object = 9u8;
global T plain(in T value) { return value; }
global T later<T>(in T value) { return value; }
global T shared_first<T>(in T value), shared_second<T>(in T value);
global T shared_first<T>(in T value) { return value; }
global T shared_second<T>(in T value) { return value; }
$::static_assert(first(300u32) + second(40u8) + third(500u16) == 840u32,
    "shared result did not resolve independently");
$::static_assert(plain(4u8) + later(300u32) == 304u32 && sizeof(object) == 1uptr,
    "later generic result or object type leaked");
$::static_assert(shared_first(300u32) + shared_second(400u16) == 700u32,
    "sibling generic results failed");
struct Record { u32 value; } first_record, second_record;
$::static_assert(sizeof(first_record) == 4uptr && sizeof(second_record) == 4uptr,
    "nongeneric shared record definition was duplicated");
]=] 0)

check(declarator_shared_macro_once [=[
typedef u8 T;
[[macro]] static $::meta::tokens shared(in $::meta::tokens input) {
    $::meta::note($::meta::span(input), "shared-result-once");
    return input;
}
global shared!(T) first<T>(in T value), second(in T value), third<T>(in T value);
global T first<T>(in T value) { return value; }
global T second(in T value) { return value; }
global T third<T>(in T value) { return value; }
$::static_assert(first(300u32) + second(40u8) + third(500u16) == 840u32,
    "shared macro result type was frozen");
]=] 0 "note: shared-result-once")

check(declarator_shared_owner_once [=[
typedef u8 T;
[[syntax_expander]] static $::meta::tokens alignment(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), "shared-alignment-once");
    return $::quote { 16uptr };
}
syntax Alignment : expression { prefix "alignment"; match input:paren; expand alignment; }
syntax Alignment;
global struct { u8 bytes[alignment(unused!())]; T value; } *first<T>(), *second<T>();
]=] 0 "note: shared-alignment-once")

check(declarator_shared_parsed_type [=[
typedef u8 T;
[[syntax_expander]] static $::meta::tokens fixed(in $::meta::syntax_match input) {
    $::meta::syntax type = $::meta::parse("type", $::quote { T }, $::syntax::context(input));
    return $::quote { global $::unquote(type) first<T>(in T value), second<T>(in T value); };
}
syntax Fixed : item { prefix "fixed"; match input:paren; expand fixed; }
syntax Fixed;
fixed()
global u8 first<T>(in T value) { return (u8)value; }
global u8 second<T>(in T value) { return (u8)value; }
$::static_assert(first(300u32) == 44u8 && second(301u32) == 45u8 &&
    sizeof(first(300u32)) == 1uptr && sizeof(second(301u32)) == 1uptr,
    "shared replay retargeted an already parsed type");
]=] 0)

foreach(projection structured tokens)
    set(emit "$::quote { $::unquote(node) }")
    if(projection STREQUAL tokens)
        set(emit "$::meta::tokens(node)")
    endif()
    check(declarator_result_${projection} "
typedef u8 T;
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, \"declaration\");
    return ${emit};
}
syntax Keep : item { prefix \"keep\"; match declaration:declaration; expand keep; }
syntax Keep;
keep global T plain(in T value), later<T>(in T value);
global T plain(in T value) { return value; }
global T later<T>(in T value) { return value; }
$::static_assert(plain(40u8) + later(300u32) == 340u32, \"copied shared result froze a binding\");
" 0)
endforeach()

check(declarator_projected_type_binding [=[
typedef u8 T;
[[syntax_expander]] static $::meta::tokens declare(in $::meta::syntax_match input) {
    $::meta::syntax original = $::meta::parse("declaration", $::quote { T original(in T value); },
        $::syntax::context(input));
    $::meta::tokens type = $::meta::slice($::meta::tokens(original), 0uptr, 1uptr);
    return $::quote { global $::unquote(type) shifted<T>(in T value); };
}
syntax Declare : item { prefix "declare"; match input:paren; expand declare; }
syntax Declare;
declare()
global u8 shifted<T>(in T value) { return (u8)value; }
$::static_assert(shifted(300u32) == 44u8 && sizeof(shifted(300u32)) == 1uptr,
    "a copied bound type token acquired an unrelated header's generic");
]=] 0)

# Reconstructing one sibling must not invalidate the remaining original
# declarators' interpretations of the shared result specifier.
foreach(first_name plain renamed)
    check(declarator_edited_sibling_${first_name} "
typedef u8 T;
[[syntax_expander]] static $::meta::tokens rewrite(in $::meta::syntax_match input) {
    $::meta::tokens source = $::meta::tokens($::syntax::node(input, \"declaration\"));
    $::meta::tokens type = $::meta::slice(source, 0uptr, 1uptr);
    $::meta::tokens rest = $::meta::slice(source, 2uptr, $::meta::len(source) - 2uptr);
    return $::quote { $::unquote(type) ${first_name} $::unquote(rest) };
}
syntax Rewrite : item { prefix \"rewrite\"; match declaration:declaration; expand rewrite; }
syntax Rewrite;
rewrite T plain(in T value), later<T>(in T value), final<T>(in T value);
T ${first_name}(in T value) { return value; }
T later<T>(in T value) { return value; }
T final<T>(in T value) { return value; }
$::static_assert(${first_name}(40u8) + later(300u32) + final(500u16) == 840u32,
    \"edited sibling lost another declarator's result scope\");
" 0)
endforeach()

foreach(edit drop reverse)
    set(tail "")
    set(plain_definition "")
    if(edit STREQUAL reverse)
        set(tail ", $::unquote($::meta::tokens($::meta::child(list, 0uptr)))")
        set(plain_definition "T plain(in T value) { return value; }")
    endif()
    check(declarator_${edit}_sibling "
typedef u8 T;
[[syntax_expander]] static $::meta::tokens rewrite(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, \"declaration\");
    $::meta::syntax list = $::meta::child(node, 1uptr);
    return $::quote { $::unquote($::meta::tokens($::meta::child(node, 0uptr)))
        $::unquote($::meta::tokens($::meta::child(list, 2uptr))) ${tail}; };
}
syntax Rewrite : item { prefix \"rewrite\"; match declaration:declaration; expand rewrite; }
syntax Rewrite;
rewrite T plain(in T value), later<T>(in T value);
${plain_definition}
T later<T>(in T value) { return value; }
$::static_assert(later(300u32) == 300u32 && sizeof(later(300u32)) == 4uptr,
    \"surviving declarator lost shared result ownership\");
" 0)
endforeach()

check(named_header_scope [=[
struct Result { u64 outer; };
static struct Result { T value; } *first<T>(in T value) {
    struct Result *result = (void *)0uptr;
    return result;
}
static struct Result { T value; } *second<T>(in T value) {
    struct Result *result = (void *)0uptr;
    return result;
}
static enum Code { code = N } first_code<u32 N>() { return code; }
static enum Code { code = N + 1 } second_code<u32 N>() { return code; }
$::static_assert(sizeof(first(7u16)->value) == 2uptr &&
    sizeof(second(7u32)->value) == 4uptr && sizeof(struct Result) == 8uptr,
    "header tags leaked or lost their local references");
$::static_assert((u32)first_code<3u32>() == 3u32 && (u32)second_code<5u32>() == 6u32,
    "header enumerators leaked between functions");
]=] 0)

check(named_header_tag_escape [=[
static struct Private { T value; } *make<T>() { return (void *)0uptr; }
struct Private escaped;
]=] 1 "error: object has incomplete type")
check(named_header_enumerator_escape [=[
static enum Code { code = N } make<u32 N>() { return code; }
global u32 entry() { return code; }
]=] 1 "error: unresolved name 'code'")

# Nominal ownership must survive sibling copies and repeated explicit header
# parsing. Matching display spelling/layout is not matching type identity.
set(header_identity_prefix "#include \"${CMAKE_CURRENT_LIST_DIR}/syntax_header_nominals.x\"\nstatic u32 same_header_type<T>(in T left, in T right) { return 1u32; }\n")
foreach(category named_copy enum_copy)
    if(category STREQUAL named_copy)
        set(application "named_copy(7u32)")
    else()
        set(application "enum_copy<7u32>()")
    endif()
    check(header_identity_reuse_${category} "${header_identity_prefix}
global u32 identity_entry() {
    return same_header_type(HeaderNominals::Composed::${application},
                            HeaderNominals::Composed::${application});
}" 0)
    foreach(other Structured Projected)
        check(header_identity_sibling_${category}_${other} "${header_identity_prefix}
global u32 identity_entry() {
    return same_header_type(HeaderNominals::Composed::${application},
                            HeaderNominals::${other}::${application});
}" 1 "error: conflicting deductions for generic type parameter 'T'")
    endforeach()
endforeach()

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
        "static u32 test<params!(u32 N)>()"
        "static u32 test<params!(u32 N)>() [[params!(noinline)]]")
    string(MD5 case "${header}")
    check(captured_value_header_${case} "${compose}
compose ${header} { return N; }
$::static_assert(test<7u32>() == 7u32, \"value header generic was lost\");" 0)
endforeach()

foreach(category declaration function_decl)
    foreach(header
            "static T ignored<unknown!(T)>(in T value)"
            "static T ((ignored<unknown!(T)>))(in T value)"
            "static T (*ignored<unknown!(T)>(in T value))(unknown!())"
            "[[unknown!(noinline)]] static T ignored<unknown!(T)>(in T value)")
        string(MD5 case "${category}-${header}")
        check(deferred_prototype_${case} "
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
    $::static_assert($::meta::is_kind($::syntax::node(input, \"value\"), \"deferred\"),
                    \"a header-dependent prototype was classified early\");
    return $::quote {};
}
syntax Drop : item { prefix \"drop\"; match value:${category}; expand drop; }
syntax Drop;
drop ${header};
global u32 entry() { return 7u32; }" 0)
    endforeach()
    foreach(projection structured tokens)
        set(reparse "$::quote { $::unquote(original) }")
        set(emit "$::quote { $::unquote(parsed) }")
        if(projection STREQUAL tokens)
            set(reparse "$::meta::tokens(original)")
            set(emit "$::meta::tokens(parsed)")
        endif()
        foreach(shadow "" "typedef u8 T;")
            string(MD5 case "${category}-${projection}-${shadow}")
            check(prototype_roundtrip_${case} "
${shadow}
[[macro]] static $::meta::tokens params(in $::meta::tokens input) {
    $::meta::note($::meta::span(input), \"prototype-generic-fragment\");
    return input;
}
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"prototype-owner\");
    $::meta::syntax original = $::syntax::node(input, \"value\");
    $::meta::syntax parsed = $::meta::parse(\"declaration\", ${reparse}, $::syntax::context(input));
    $::static_assert($::meta::is_kind(parsed, \"deferred\"), \"prototype parse ran its generic fragment\");
    parsed = $::meta::parse(\"function_decl\", $::quote { $::unquote(parsed) }, $::syntax::context(input));
    return ${emit};
}
syntax Keep : item { prefix \"keep\"; match value:${category}; expand keep; }
syntax Keep;
keep global T identity<params!(T)>(in T value);
global T identity<T>(in T value) { return value; }
$::static_assert(identity(300u32) == 300u32, \"prototype lost its generic type\");
global u32 entry() { return identity(300u32); }" 0
                "note: prototype-owner" "note: prototype-generic-fragment")
        endforeach()
    endforeach()
endforeach()

# The broader category must not obtain provisional ordinary object types or
# consume a function definition just because its header contains a macro.
foreach(declaration
        "static T object<unknown!(T)>;"
        "static T (*object<unknown!(T)>)(unknown!());"
        "static absent::T ignored<unknown!(T)>();"
        "static T ignored<unknown!(T)>(in T value) { unknown!(); }"
        "static u32 ignored(unknown!()) { unknown!(); }"
        "static T first(in T value), second<unknown!(T)>(in T value);")
    string(MD5 case "${declaration}")
    check(reject_provisional_declaration_${case} "
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax Drop : item { prefix \"drop\"; match value:declaration; expand drop; }
syntax Drop;
drop ${declaration}" 1 "error: syntax-match error for active prefix")
endforeach()

foreach(header
        "static T test<params!(T)>(in T value)"
        "static T test<params!(T)>(in T value) [[params!(noinline)]]")
    string(MD5 case "${header}")
    check(captured_type_header_${case} "${compose}
compose ${header} { return value; }
$::static_assert(test(7u32) == 7u32, \"type header generic was lost\");" 0)
endforeach()

check(captured_shadowed_alias "${compose}
typedef u32 T();
compose static T test<params!(T)>(in T value) { return value; }
$::static_assert(test(9u32) == 9u32, \"late generic did not shadow the callable alias\");" 0)

check(captured_angle_value_alias "${compose}
typedef u32 Count;
compose static u32 test<Count N, params!(T)>() { return N; }
$::static_assert(test<7u32, u32>() == 7u32, \"pending angle header rejected a scalar alias\");" 0)

check(captured_tag_types "${compose}
struct Pair { u32 value; };
enum Code { code = 4 };
compose static struct Pair record<params!(u32 N)>(in u32 value) {
    struct Pair result = { value + N }; return result;
}
compose static enum Code enumeration<params!(u32 N)>() { return code; }
$::static_assert(record<3u32>(4u32).value == 7u32, \"record header classification failed\");
$::static_assert((u32)enumeration<3u32>() == 4u32, \"enum header classification failed\");" 0)

check(captured_qualified_alias "${compose}
namespace Types { typedef u32 Value; }
compose static Types::Value test<params!(u32 N)>(in Types::Value value) {
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
static u32 first<params!(T)>() [[aligned(known(sizeof(struct Tag)))]] { return 1u32; }
static u32 second<params!(T)>() [[aligned(known(sizeof(Types::Value)))]] { return 2u32; }
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
        "static u32 discarded<unknown!(u32 N)>()"
        "static T discarded<unknown!(T)>(in T value)"
        "[[unknown!(noinline)]] static u32 discarded()"
        "static u32 (*discarded<unknown!(T)>(in u32 value))(unknown!())")
    string(MD5 case "${header}")
    check(discarded_header_${case} "${discard_header}
discard ${header} { completely noncore body; unknown!{discarded}; }" 0)
endforeach()

# A known ordinary attribute's arguments are balanced-token grammar, not
# binding declarations. Retain their macro input without deferring the header.
foreach(attribute "abi(unknown!(\"not-an-abi\"))" "no_sanitize(unknown!(\"bounds\"))"
                  "library::annotation(unknown!{unparsed tokens})")
    string(MD5 case "${attribute}")
    check(balanced_attribute_header_${case} "
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    $::static_assert($::meta::is_production($::syntax::node(input, \"header\"), \"function_header\"),
                    \"balanced ordinary attribute unnecessarily deferred its header\");
    return $::quote {};
}
syntax Discard : item { prefix \"discard\"; match header:function_header body:block; expand discard; }
syntax Discard;
discard static u32 discarded() [[${attribute}]] { completely noncore body; }
" 0)
endforeach()

foreach(header
        "static u32 (*not_function<unknown!(T)>)(unknown!())"
        "static T not_function<unknown!(T)>"
        "static u32 unknown!(not_function)<unknown!(T)>"
        "[[, noinline]] static u32 not_function<unknown!(T)>()"
        "static u32 not_function<>(unknown!())"
        "static T not_function<unknown!(T)(in T value)"
        "static absent::T not_function<unknown!(T)>()")
    string(MD5 case "${header}")
    check(rejected_header_${case} "${discard_header}
discard ${header} { unknown!{discarded}; }" 1 "error: syntax-match error for active prefix")
endforeach()

foreach(category function_decl function_def)
    set(suffix ";")
    set(after "global T kept<T>(in T value) { return value; }")
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
copy global T kept<params!(T)>(in T value) ${suffix}
${after}
$::static_assert(kept(11u32) == 11u32, \"function category lost its generic\");" 0)
endforeach()

foreach(category function function_raw)
    check(raw_${category}_discard "
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax Drop : item { prefix \"drop\"; match body:${category}; expand drop; }
syntax Drop;
drop static T ignored(in T value) [[unknown!(noinline)]] { not core syntax; unknown!{}; }
drop static T ignored_angle<unknown!(T)>(in T value) { not core syntax; unknown!{}; }" 0)
    check(raw_${category}_copy "
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::syntax::capture(input, \"body\");
}
syntax Copy : item { prefix \"copy\"; match body:${category}; expand copy; }
syntax Copy;
copy static T kept<params!(T)>(in T value) { return value; }
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
[[aligned(${expression})]] static u32 test<bad!(T)>() { return 0u32; }"
        1 "note: earlier-header-owner" "error: division by zero")
endforeach()

check(generic_before_parameter "${prefix}
static u32 test<bad!(T)>(in u32 values[notice(unknown!{discarded})]) { return 0u32; }"
    1 "error: division by zero" "note: earlier-header-owner")

foreach(siblings "" "first<T>(in T value)," "first<T>(in T value), second(in u8 value),"
                 "object = 1u32,")
    string(MD5 case "${siblings}")
    check(declarator_header_order_${case} "${prefix}
global u32 ${siblings}
    later<bad!(T)>(in u8 values[notice(unknown!{discarded})]);"
        1 "error: division by zero" "note: earlier-header-owner")
endforeach()

check(declarator_header_shared_once "${prefix}
typedef u8 T;
[[macro]] static $::meta::tokens result(in $::meta::tokens input) {
    $::meta::note($::meta::span(input), \"shared-before-siblings\");
    return input;
}
[[macro]] static $::meta::tokens generic_names(in $::meta::tokens input) {
    $::meta::note($::meta::span(input), \"later-generic-names\");
    return input;
}
global result!(T) first<T>(in T value),
    later<generic_names!(T)>(in u8 values[notice(unknown!{discarded})]);"
    0 "note: shared-before-siblings" "note: later-generic-names" "note: earlier-header-owner")

check(generic_before_parameter_fragment "${prefix}
[[macro]] static $::meta::tokens parameter(in $::meta::tokens input) {
    return $::quote { in u32 value[notice(unknown!{discarded})] };
}
static u32 test<bad!(T)>(parameter!()) { return 0u32; }"
    1 "error: division by zero" "note: earlier-header-owner")

check(nested_output "${prefix}
[[syntax_expander]] static $::meta::tokens outer(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"outer-header-owner\");
    return $::quote { notice(unknown!{discarded}) };
}
syntax Outer : expression { prefix \"outer\"; match input:paren; expand outer; }
syntax Outer;
[[aligned(outer())]] static u32 test<bad!(T)>() { return 0u32; }"
    1 "note: outer-header-owner" "note: earlier-header-owner" "error: division by zero")

check(nested_macro_before_later_fragment "${prefix}
[[syntax_expander]] static $::meta::tokens outer(in $::meta::syntax_match input) {
    $::syntax::note($::syntax::span(input), \"outer-header-owner\");
    return $::quote { params!(notice(unknown!{discarded})) };
}
syntax Outer : expression { prefix \"outer\"; match input:paren; expand outer; }
syntax Outer;
[[aligned(outer())]] static u32 test<bad!(T)>() { return 0u32; }"
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
static T first<params!(T)>(in T value) [[aligned(capture(sizeof(T *)))]] { return value; }
static T second<params!(T)>(in T value) [[aligned(capture((T)16u32))]] { return value; }
static u32 third<params!(u32 N)>() [[aligned(capture(N))]] { return 3u32; }
global u32 entry() { return first(1u32) + second(2u32) + third<16u32>(); }" 0)

foreach(projection structured tokens)
    set(emit "$::quote { $::unquote(node) }")
    if(projection STREQUAL tokens)
        set(emit "$::meta::tokens(node)")
    endif()
    check(declarator_deferred_copy_${projection} "${prefix}${capture}
typedef u8 T;
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, \"declaration\");
    return ${emit};
}
syntax Keep : item { prefix \"keep\"; match declaration:declaration; expand keep; }
syntax Keep;
keep global T first<params!(T)>(in T value),
    later<params!(T)>(in T value, in u8 data[capture(sizeof(T))]);
global T first<T>(in T value) { return value; }
global T later<T>(in T value, in u8 *data) { return value; }
static u32 check() { u8 data[4uptr]; return first(300u32) + later(400u32, data); }
$::static_assert(check() == 700u32, \"deferred declaration froze its shared result\");" 0)
endforeach()

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
static T test<params!(T)>(in T value) [[aligned(2u32 * sum())]] { return value; }
global u32 entry() { return test(3u32); }" 0)

check(bounded_trailing_tokens "${prefix}
[[syntax_expander]] static $::meta::tokens extra(in $::meta::syntax_match input) {
    return $::quote { 16u32; };
}
syntax Extra : expression { prefix \"extra\"; match input:paren; expand extra; }
syntax Extra;
static u32 test<params!(T)>() [[aligned(extra())]] { return 0u32; }"
    1 "prepared expression must contain one assignment expression")

check(body_not_prepared "${prefix}
static T test<params!(T)>(in T value) [[aligned(notice())]] {
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
owner [[aligned(notice(unknown!{discarded}))]] static u32 test<bad!(T)>() { return 0u32; }"
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
compose static T test<params!(T)>(in T value) { return value; }
$::static_assert(test(17u32) == 17u32, "header parse roundtrip lost its generic");
]=] 0)

check(decorated_core_header [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    $::static_assert(!$::meta::is_kind(header, "deferred"), "original header was not settled");
    header = $::meta::parse("function_header", $::quote {
        [[params!(noinline)]] $::unquote(header) [[params!(cold)]]
    }, $::syntax::context(input));
    $::static_assert($::meta::is_kind(header, "deferred"), "opaque decoration was not deferred");
    return $::quote { $::unquote(header) $::unquote($::syntax::capture(input, "body")) };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
compose static u32 kept<u32 N>(in u32 value) { return value; }
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
compose static u32 kept<params!(T)>() [[aligned(notice(unknown!{discarded}))]];"
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
compose static T kept<params!(T)>(in T value) { T copy = value; return copy; }
$::static_assert(kept(23u32) == 23u32, "body fragment lost header generic bindings");
]=] 0)

foreach(tag struct union)
    foreach(generic "T" "params!(T)")
        string(MD5 case "${tag}-${generic}")
        check(inline_tag_generic_${case} "
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
static ${tag} Result { u32 value; } make<${generic}>(in T value) {
    ${tag} Result result = { (u32)value }; return result;
}
$::static_assert(make(29u32).value == 29u32, \"inline result tag hid header generics\");" 0)
    endforeach()
endforeach()

check(inline_enum_generic [=[
[[macro]] static $::meta::tokens params(in $::meta::tokens input) { return input; }
static enum Result { value = 31 } make<params!(T)>(in T input) { return value; }
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
compose static struct Result { u32 value; } make<params!(T)>(in T value) {
    struct Result result = { (u32)value }; return result;
}
$::static_assert(make(37u32).value == 37u32, \"captured inline result tag lost its binding\");" 0)

check(inline_tag_owner_order "${prefix}
static struct Result { u32 values[notice(unknown!{discarded})]; } make<bad!(T)>(in T value)
    { struct Result result = {}; return result; }"
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
make<params!(T)>(in T value) {
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
make<bad!(T)>(in T value) { struct Result result = { 0u32 }; return result; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(inline_enum_owner_order "${prefix}
static enum Result { first = notice(unknown!{discarded}) }
make<bad!(T)>(in T value) { return first; }"
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
static T (named!(identity)<params!(T)>)(in T value)
    [[aligned(notice(unknown!{discarded}))]] { return value; }
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
[[aligned(${expression} + notice(unknown!{discarded}))]]
static T test<bad!(T)>(in T value) {
    return value;
}" 1 "note: earlier-header-owner" "error: division by zero")
    check(opaque_header_type_valid_${case} "${prefix}
static u32 alignment<U>() { return 16u32; }
static T test<params!(T)>(in T value)
    [[aligned((${expression} != 0uptr ? 16u32 : 16u32) +
               notice(unknown!{discarded}) - 16u32)]] {
    return value;
}
$::static_assert(test(300u32) == 300u32, \"opaque type damaged final generic bindings\");"
        0 "note: earlier-header-owner")
endforeach()

foreach(expression "sizeof(T [notice(unknown!{discarded})])"
                   "sizeof(T (*)(in u32 values[notice(unknown!{discarded})]))")
    string(MD5 case "${expression}")
    check(opaque_type_owner_order_${case} "${prefix}
[[aligned(${expression})]] static T test<bad!(T)>(in T value) { return value; }"
        1 "note: earlier-header-owner" "error: division by zero")
endforeach()

check(opaque_header_value_order "${prefix}
[[aligned(sizeof(N params!(+ 1u32)) + notice(unknown!{discarded}))]]
static u32 test<bad!(u32 N)>() { return N; }"
    1 "note: earlier-header-owner" "error: division by zero")

check(opaque_header_value_valid "${prefix}
static u32 test<params!(u32 N)>()
    [[aligned(sizeof(N params!(+ 1u32)) + notice(unknown!{discarded}) - sizeof(u32))]] { return N; }
$::static_assert(test<300u32>() == 300u32, \"value generic was forced into type grammar\");"
    0 "note: earlier-header-owner")

foreach(expression "sizeof(N * notice(unknown!{discarded}))"
                   "sizeof(Target(notice(unknown!{discarded})))")
    string(MD5 case "${expression}")
    check(opaque_header_expression_owner_${case} "${prefix}
static u32 Target(in u32 value) { return value; }
static u32 test<params!(u32 N)>() [[aligned(${expression} + 16u32 - sizeof(u32))]] { return N; }
$::static_assert(test<300u32>() == 300u32, \"type probing damaged an expression owner\");"
        0 "note: earlier-header-owner")
endforeach()

check(opaque_probe_split_token_position "${prefix}
static u32 alignment<U>() { return 16u32; }
[[aligned(sizeof(T [alignment::<alignment::<u32>>() + notice(unknown!{discarded})]))]]
static T test<bad!(T)>(in T value) { return value; }"
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
${wrapper}static T test<params!(T)>(in T value)
    [[aligned(Helpers::alignment::<16u32, T(Param(params!(in u32)))>())]] { return value; }
$::static_assert(test(300u32) == 300u32, \"known type slot lost nested callable grammar\");" 0)
endforeach()

check(known_generic_type_slot_prefix_priority "${prefix}
static u32 alignment<U>() { return 16u32; }
[[aligned(alignment::<notice(unknown!{discarded})>())]]
static T test<bad!(T)>(in T value) { return value; }"
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
