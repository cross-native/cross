// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens multiply(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    // A splice is one token tree through copying, slicing and concatenation.
    $::meta::tokens head = $::quote { $::unquote(value) };
    head = $::meta::slice(head, 0uptr, 1uptr);
    return $::meta::concat(head, $::quote { * 3u32 });
}
syntax Multiply : expression {
    prefix "multiplied"; match "(" value:expr ")"; expand multiply;
}

[[syntax_expander]] static $::meta::tokens parse_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    return $::quote { $::unquote(parsed) + 1u32 };
}
syntax ParseThenSplice : expression {
    prefix "parse_splice"; match "(" value:expr ")"; expand parse_then_splice;
}

[[syntax_expander]] static $::meta::tokens project_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    return $::meta::concat($::meta::tokens(parsed), $::quote { + 1u32 });
}
syntax ProjectThenSplice : expression {
    prefix "project_splice"; match "(" value:expr ")"; expand project_then_splice;
}

[[syntax_expander]] static $::meta::tokens inspect_without_expanding(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) + 1u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    // The nested invocation belongs to the discarded node, not this owner.
    return $::quote { 9u32 };
}
syntax InspectWithoutExpanding : expression {
    prefix "inspect_splice"; match "(" value:expr ")"; expand inspect_without_expanding;
}

[[syntax_expander]] static $::meta::tokens active_type_expression(in $::meta::syntax_match input) {
    return $::quote { 7u16 };
}
syntax ActiveTypeExpression : expression {
    prefix "ProbeType"; match "*"; expand active_type_expression;
}

[[syntax_expander]] static $::meta::tokens transplant_statement(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax parsed = $::meta::parse("stmt",
        $::quote { $::unquote(body) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "statement"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(body) };
}
syntax Transplant : statement {
    prefix "transplant"; match body:stmt; expand transplant_statement;
}
[[syntax_expander]] static $::meta::tokens inner_statement(in $::meta::syntax_match input) {
    return $::quote { u32 $::unquote($::syntax::capture(input, "name")) = 13u32; };
}
syntax InnerStatement : statement { prefix "inner_statement"; match name:ident ";"; expand inner_statement; }
[[syntax_expander]] static $::meta::tokens transplant_inner(in $::meta::syntax_match input) {
    $::meta::syntax root = $::syntax::node(input, "body");
    $::meta::syntax ordinary = $::meta::child(root, 0uptr);
    $::meta::syntax extension = $::meta::child(ordinary, 0uptr);
    if (!$::meta::is_kind(extension, "extension"))
        return $::quote { public_schema_failure(); };
    $::meta::syntax parsed = $::meta::parse("stmt",
        $::quote { $::unquote(extension) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "statement") ||
        !$::meta::is_kind($::meta::child(parsed, 0uptr), "extension"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(extension) };
}
syntax TransplantInner : statement {
    prefix "transplant_inner"; match body:stmt; expand transplant_inner;
}
[[syntax_expander]] static $::meta::tokens shadow_transplant(in $::meta::syntax_match input) {
    $::meta::tokens local = $::meta::call_site($::meta::parse("amount"));
    return $::quote { {
        u32 $::unquote(local) = 99u32;
        $::unquote($::syntax::node(input, "body"))
        if ($::unquote($::syntax::capture(input, "result")) != 4u32) return 0u32;
    } };
}
syntax ShadowTransplant : statement {
    prefix "shadow_transplant"; match result:ident body:stmt; expand shadow_transplant;
}
[[syntax_expander]] static $::meta::tokens transplant_type(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("type",
        $::quote { $::unquote(type) }, $::syntax::context(input));
    $::meta::syntax specifier = $::meta::child(
        $::meta::child($::meta::child(parsed, 0uptr), 0uptr), 0uptr);
    if (!$::meta::is_production(parsed, "type_name") ||
        !$::meta::is_production(specifier, "type_specifier") ||
        (!$::meta::is_production($::meta::child(specifier, 0uptr), "type_name") &&
         !$::meta::is_kind($::meta::child(specifier, 0uptr), "deferred")))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(parsed) $::unquote($::syntax::capture(input, "name")); };
}
syntax TransplantType : statement {
    prefix "transplant_type"; match name:ident value:type ";"; expand transplant_type;
}
[[syntax_expander]] static $::meta::tokens shadow_type(in $::meta::syntax_match input) {
    $::meta::tokens alias = $::meta::call_site($::meta::parse("CapturedAlias"));
    $::meta::tokens name = $::syntax::capture(input, "name");
    return $::quote { {
        typedef u32 $::unquote(alias);
        $::unquote($::syntax::node(input, "value")) $::unquote(name);
        if (sizeof($::unquote(name)) != 2uptr) return 0u32;
    } };
}
syntax ShadowType : statement {
    prefix "shadow_type"; match name:ident value:type ";"; expand shadow_type;
}
[[syntax_expander]] static $::meta::tokens transplant_specifier(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    $::meta::syntax specifier = $::meta::child(
        $::meta::child($::meta::child(type, 0uptr), 0uptr), 0uptr);
    return $::quote { $::unquote(specifier) $::unquote($::syntax::capture(input, "name")); };
}
syntax TransplantSpecifier : statement {
    prefix "transplant_specifier"; match name:ident value:type ";"; expand transplant_specifier;
}
[[syntax_expander]] static $::meta::tokens copy_declaration(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax CopyDeclaration : statement {
    prefix "copy_declaration"; match body:declaration; expand copy_declaration;
}
syntax CopyExternal : item {
    prefix "copy_external"; match body:declaration; expand copy_declaration;
}
syntax CopyExternal;
copy_external global u32 external_spliced_value = 17u32;
copy_external typedef u16 ExternalSplicedType;
[[syntax_expander]] static $::meta::tokens move_external_declaration(
    in $::meta::syntax_match input) {
    return $::quote { namespace Destination {
        $::unquote($::syntax::node(input, "body"))
    } };
}
syntax MoveExternal : item {
    prefix "move_external"; match body:declaration; expand move_external_declaration;
}
namespace Source {
    typedef u16 OriginType;
    namespace Destination { typedef u32 OriginType; }
    syntax MoveExternal;
    move_external global OriginType moved;
}
[[macro]] static $::meta::tokens declare_spliced(in $::meta::tokens name) {
    return $::quote { u32 $::unquote(name) = 11u32; };
}
[[macro]] static $::meta::tokens declarator_name(in $::meta::tokens name) {
    return $::quote { $::unquote(name) };
}
[[macro]] static $::meta::tokens declarator_pointer(in $::meta::tokens name) {
    return $::quote { *$::unquote(name) };
}
[[macro]] static $::meta::tokens declarator_array(in $::meta::tokens ignored) {
    return $::quote { [2] };
}
[[macro]] static $::meta::tokens declarator_function(in $::meta::tokens name) {
    return $::quote { (in u32 $::unquote(name)) };
}
[[macro]] static $::meta::tokens parameter_fragment(in $::meta::tokens name) {
    return $::quote { in u32 $::unquote(name) };
}
[[macro]] static $::meta::tokens parameter_list_fragment(in $::meta::tokens input) {
    return input;
}
[[macro]] static $::meta::tokens nested_parameter_list(in $::meta::tokens input) {
    return $::quote { parameter_list_fragment!($::unquote(input)) };
}
[[syntax_expander]] static $::meta::tokens deferred_header_value(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    if (!$::meta::is_kind(value, "deferred"))
        $::syntax::error($::syntax::span(input), "header dependency was classified too soon");
    return $::quote { $::unquote(value) };
}
syntax DeferredHeaderValue : expression {
    prefix "header_value"; match "(" value:expr ")"; expand deferred_header_value;
}
syntax DeferredHeaderValue;
[[syntax_expander]] static $::meta::tokens captured_header(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    if (!$::meta::is_kind(header, "deferred"))
        $::syntax::error($::syntax::span(input), "generated generic header did not defer");
    return $::quote { $::unquote(header) $::unquote($::syntax::capture(input, "body")) };
}
syntax CapturedHeader : item {
    prefix "captured_header"; match header:function_header body:block; expand captured_header;
}
syntax CapturedHeader;
static uptr header_alignment<U>() { return sizeof(U); }
captured_header
[[noinline]] static T captured_header_identity(in T value)
    [[aligned(header_value(header_alignment::<T (parameter_list_fragment!(*))(in u32)>())),
      generic(parameter_list_fragment!(T))]] {
    return value;
}
captured_header [[noinline]] static u32 captured_header_value(in u32 value)
    [[aligned(header_value(N)), generic(parameter_list_fragment!(u32 N))]] {
    return value + N;
}
captured_header [[noinline]] static struct InlineHeaderResult { uptr value; }
captured_inline_header(in T value) [[generic(parameter_list_fragment!(T))]] {
    struct InlineHeaderResult result = { (uptr)value + 1uptr };
    return result;
}
[[syntax_expander]] static $::meta::tokens raw_inline_header(in $::meta::syntax_match input) {
    return $::syntax::capture(input, "body");
}
syntax RawInlineHeader : item {
    prefix "raw_inline_header"; match body:function_raw; expand raw_inline_header;
}
syntax RawInlineHeader;
raw_inline_header [[noinline]] static enum InlineRawResult [[underlying(u32)]] { raw_result_base = 8u32 }
copied_inline_enum(in T value) [[generic(parameter_list_fragment!(T))]] {
    return (enum InlineRawResult)((u32)value + raw_result_base);
}
namespace HeaderFragments { typedef u32 Value; }
[[noinline]] static T header_fragment_identity(in T value)
    [[parameter_list_fragment!(generic(T))]] { return value; }
[[noinline]] static T angle_fragment_identity<parameter_list_fragment!(T)>(in T value) {
    return value;
}
namespace GroupedHeaders {
    typedef u8 T;
    [[noinline]] static T ((identity<parameter_list_fragment!(T)>))(in T (value)) {
        return value;
    }
}
global volatile u32 header_runtime_seed = 5u32;
[[noinline]] static u32 suffix_function declarator_function!(value) {
    return value + 3u32;
}
[[noinline]] static u32 parameter_function(parameter_fragment!(value)) {
    return value + 2u32;
}
[[syntax_expander]] static $::meta::tokens copy_parameter_header(
    in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    if (!$::meta::is_kind(header, "deferred"))
        $::syntax::error($::syntax::span(input), "function header was parsed too early");
    return $::quote {
        $::unquote(header)
        $::unquote($::syntax::capture(input, "body"))
    };
}
syntax CopyParameterHeader : item {
    prefix "copy_parameter_header";
    match header:function_header body:block;
    expand copy_parameter_header;
}
syntax CopyParameterHeader;
copy_parameter_header [[noinline]] static u32 header_function(parameter_fragment!(value)) {
    return value + 4u32;
}
copy_parameter_header [[noinline]] static u32 empty_parameter_function(parameter_list_fragment!()) {
    return header_runtime_seed + 1u32;
}
copy_parameter_header [[noinline]] static u32 void_parameter_function(void parameter_list_fragment!()) {
    return header_runtime_seed + 2u32;
}
copy_parameter_header [[noinline]] static u32 list_parameter_function(
    nested_parameter_list!(in u32 left, in u32 right)) { return left + right; }
typedef u32 (*EmptyCallback)(nested_parameter_list!(void));
[[noinline]] static u32 dispatch_empty(in EmptyCallback function) { return function(); }
#if $::has_feature($::feature::variadics)
#ifdef CUSTOM_SYNTAX_ABI
// The odd-register model intentionally has no variadic transport. Exercise an
// explicit supported ABI boundary without inventing a fallback for that model.
#define PARAMETER_VARIADIC_ABI [[abi(HOST_ABI)]]
#else
#define PARAMETER_VARIADIC_ABI
#endif
copy_parameter_header PARAMETER_VARIADIC_ABI [[noinline]] static u32 variadic_parameter_function(
    in u32 value, parameter_list_fragment!(...)) { return value + header_runtime_seed; }
typedef u32 (*VariadicCallback)(in u32 value, nested_parameter_list!(...)) PARAMETER_VARIADIC_ABI;
[[noinline]] static u32 dispatch_variadic(in VariadicCallback function, in u32 value) {
    return function(value, 9u32);
}
#endif
#ifdef CUSTOM_SYNTAX_ABI
[[syntax_expander]] static $::meta::tokens assign_header_abi(in $::meta::syntax_match input) {
    return $::quote {
        [[abi($::unquote($::syntax::capture(input, "abi")))]]
        $::unquote($::syntax::node(input, "header")) [[noinline]]
        $::unquote($::syntax::capture(input, "body"))
    };
}
syntax AssignHeaderAbi : item {
    prefix "assign_header_abi";
    match abi:literal header:function_header body:block;
    expand assign_header_abi;
}
syntax AssignHeaderAbi;
assign_header_abi "stack_result_abi"
static u32 stack_header_function(parameter_fragment!(value)) { return value + 9u32; }
assign_header_abi "memory_result_abi"
static struct HeaderMemoryResult { u64 low; u64 high; }
memory_header_function(in T value) [[generic(parameter_list_fragment!(T))]] {
    struct HeaderMemoryResult result = { (u64)value + 10u64, (u64)value + 11u64 };
    return result;
}
assign_header_abi "stack_result_abi"
static u32 (captured_stack_header_value)(in u32 value)
    [[aligned(header_value(N)), generic(parameter_list_fragment!(u32 N))]] { return value + N; }
assign_header_abi "memory_result_abi"
static struct HeaderMemoryResult ((captured_memory_header_value))(in u32 value)
    [[aligned(header_value(N)), generic(parameter_list_fragment!(u32 N))]] {
    struct HeaderMemoryResult result = { (u64)value + (u64)N, (u64)value + (u64)N + 1u64 };
    return result;
}
#endif
[[syntax_expander]] static $::meta::tokens compose_plain_header(
    in $::meta::syntax_match input) {
    $::meta::syntax header = $::meta::parse("function_header", $::quote {
        [[noinline]] $::unquote($::syntax::node(input, "header")) [[aligned(16)]]
    }, $::syntax::context(input));
    $::meta::syntax definition = $::meta::parse("function_def", $::quote {
        $::unquote(header)
        $::unquote($::syntax::capture(input, "body"))
    }, $::syntax::context(input));
    return $::quote { $::unquote(definition) };
}
syntax ComposePlainHeader : item {
    prefix "compose_plain_header";
    match header:function_header body:block;
    expand compose_plain_header;
}
syntax ComposePlainHeader;
compose_plain_header static T generic_header<T>(in T value) {
    T copy = value;
    return copy;
}
compose_plain_header static u32 recursive_header(in u32 value) {
    if (value <= 1u32) return 1u32;
    return value * recursive_header(value - 1u32);
}
[[syntax_expander]] static $::meta::tokens copy_parameter_prototype(
    in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    if (!$::meta::is_kind(body, "deferred"))
        $::syntax::error($::syntax::span(input), "prototype was parsed too early");
    return $::quote { $::unquote(body) };
}
syntax CopyParameterPrototype : item {
    prefix "copy_parameter_prototype";
    match body:function_decl;
    expand copy_parameter_prototype;
}
syntax CopyParameterPrototype;
copy_parameter_prototype static u32 prototype_function(parameter_fragment!(value));
[[noinline]] static u32 prototype_function(in u32 value) { return value + 5u32; }
[[syntax_expander]] static $::meta::tokens copy_function_definition(
    in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax CopyFunctionDefinition : item {
    prefix "copy_function_definition";
    match body:function_def;
    expand copy_function_definition;
}
syntax CopyFunctionDefinition;
copy_function_definition [[noinline]] static u32 copied_function(in u32 value) {
    return value + 6u32;
}
copy_function_definition [[noinline]] static u32 deferred_copied_function(
    parameter_fragment!(value)) {
    return value + 7u32;
}
[[macro]] static $::meta::tokens deferred_body_type(in $::meta::tokens name) {
    return $::quote { typedef u32 $::unquote(name); };
}
copy_function_definition [[noinline]] static u32 deferred_body_function(in u32 value) {
    deferred_body_type!(Local);
    Local result = value + 8u32;
    return result;
}

#include "tag_lookup.x"
#include "local_tag_scope.x"
#include "local_tag_splice.x"
#include "local_tag_projection.x"

namespace TagTreeDefinition {
    enum E [[underlying(u16)]] { TagValue };
    struct R { u16 value; };
    [[syntax_expander]] static $::meta::tokens size(in $::meta::syntax_match input) {
        $::meta::tokens name = $::syntax::capture(input, "name");
        $::meta::syntax parsed = $::meta::parse("type",
            $::quote { struct $::unquote(name) }, $::syntax::context(input));
        // The keyword's definition context must not replace the copied name's
        // invocation context, even after a public-tree parse and splice.
        return $::quote { (u32)(sizeof($::unquote(parsed)) + sizeof(enum E) +
            sizeof($::unquote($::syntax::node(input, "type")))) };
    }
    syntax Size : expression { prefix "tag_size"; match "(" name:ident "," type:type ")"; expand size; }
}
namespace TagTreeInvocation {
    enum E [[underlying(u32)]] { TagValue };
    struct R { u32 value; };
    [[noinline]] static u32 run(in u32 amount) {
        syntax TagTreeDefinition::Size;
        return amount + tag_size(R, enum E);
    }
}

namespace PatternComposition {
    syntax Fence : rule { match ";"; }
    [[syntax_expander]] static $::meta::tokens select_value(in $::meta::syntax_match input) {
        $::meta::syntax_match branch = $::syntax::at(input, "branch", 0uptr);
        if (!$::syntax::is_variant(branch, "value")) return $::quote { 3u32 };
        $::meta::syntax value = $::syntax::node(branch, "value");
        return $::quote { $::unquote(value) * 2u32 };
    }
    syntax Repeated : expression {
        prefix "pick_repeat";
        match "(" branch:choice(pairs:(parts:repeat1("x" "y")) |
            value:("x" value:expr rule(Fence))) ")";
        expand select_value;
    }
    syntax Separated : expression {
        prefix "pick_separated";
        match "(" branch:choice(pairs:(parts:separated1(number:literal, ",")) |
            value:(head:literal "," value:expr rule(Fence))) ")";
        expand select_value;
    }
    [[noinline]] static u32 run(in u32 amount) {
        syntax Repeated, Separated;
        return pick_repeat (x amount + 2u32;) +
            pick_separated (1u32, amount + 3u32;) + pick_repeat (x y x y);
    }
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (LocalTagProjection::run(11u32) != 11u32) return 0u32;
    if (LocalTagSplice::run(9u32) != 9u32) return 0u32;
    if (LocalTagScope::first(11u32) != 18u32 || LocalTagScope::second(65549u32) != 65560u32 ||
        LocalTagScope::loops() != 3u32 || LocalTagScope::unions(65549u32) != 65549u32) return 0u32;
    if (TagLookup::Invocation::run(9u32) != 9u32 ||
        TagTreeInvocation::run(7u32) != 17u32) return 0u32;
    if (PatternComposition::run(4u32) != 29u32) return 0u32;
    u32 textual_precedence = parameter_list_fragment!(1u32 + 2u32) * 3u32;
    u32 textual_value = 1u32;
    parameter_list_fragment!(textual_value += 2u32; textual_value += 3u32;)
    parameter_list_fragment!()
    u32 textual_operator = 1u32 parameter_list_fragment!(+) 2u32;
    if (textual_precedence != 7u32 || textual_value != 6u32 || textual_operator != 3u32)
        return 0u32;
#if $::has_feature($::feature::variadics)
    if (dispatch_variadic(&variadic_parameter_function, 8u32) != 13u32) return 0u32;
#endif
#ifdef CUSTOM_SYNTAX_ABI
    u32 seed = header_runtime_seed;
    struct HeaderMemoryResult memory = memory_header_function(seed);
    struct HeaderMemoryResult captured_memory = captured_memory_header_value<16u32>(seed);
    if (stack_header_function(seed) != 14u32 || memory.low != 15u64 || memory.high != 16u64 ||
        captured_stack_header_value<16u32>(seed) != 21u32 ||
        captured_memory.low != 21u64 || captured_memory.high != 22u64)
        return 0u32;
#endif
    syntax Multiply;
    syntax ParseThenSplice;
    syntax ProjectThenSplice;
    syntax InspectWithoutExpanding;
    syntax Transplant;
    syntax InnerStatement, TransplantInner;
    syntax ShadowTransplant;
    syntax TransplantType;
    syntax ShadowType;
    syntax TransplantSpecifier;
    syntax CopyDeclaration;
    {
        typedef u64 ProbeType;
        syntax ActiveTypeExpression;
        if (sizeof(ProbeType *) != sizeof(u16) || (ProbeType *) + 3u16 != 10u16 ||
            multiplied(ProbeType *) != 21u32) return 0u32;
    }
    u32 amount = 4u32;
    transplant_type attributed_pointer [[address_space(0)]] u32 *;
    attributed_pointer = ([[address_space(0)]] u32 *)&amount;
    u32 attributed_values[2] = { 5u32, 8u32 };
    transplant_type attributed_array [[address_space(0)]] u32 (*)[2u32];
    attributed_array = &attributed_values;
    if (*attributed_pointer != 4u32 || (*attributed_array)[1] != 8u32 ||
        sizeof([[atomic]] u32) != sizeof(u32) ||
        sizeof([[address_space(0)]] u32 *) != sizeof(u32 *)) return 0u32;
    transplant_type fragmented_type HeaderFragments parameter_list_fragment!(::Value);
    fragmented_type = header_runtime_seed;
    u32 (parameter_list_fragment!(*grouped_callback))(in u32 value) = &parameter_function;
    if (grouped_callback(fragmented_type) != 7u32 ||
        GroupedHeaders::identity(fragmented_type + 300u32) != 305u32) return 0u32;
    struct InlineHeaderResult inline_header_result = captured_inline_header(fragmented_type);
    if (header_fragment_identity(fragmented_type) != 5u32 ||
        angle_fragment_identity(fragmented_type) != 5u32 ||
        captured_header_identity(fragmented_type) != 5u32 ||
        captured_header_value<16u32>(fragmented_type) != 21u32 ||
        inline_header_result.value != 6uptr ||
        (u32)copied_inline_enum(fragmented_type) != 13u32) return 0u32;
    if (multiplied(amount parameter_list_fragment!(+) 2u32) != 18u32)
        return 0u32;
    u32 declarator_name!(named_by_macro) = 5u32;
    u32 declarator_pointer!(pointer_by_macro) = &amount;
    u32 array_by_macro declarator_array!() = { 2u32, 3u32 };
    copy_declaration register u32 local_spliced_value = external_spliced_value;
    copy_declaration u32 copied_array declarator_array!() = { 1u32, 8u32 };
    copy_declaration typedef u16 LocalSplicedType;
    LocalSplicedType local_spliced_alias = 6u16;
    ExternalSplicedType external_spliced_alias = 7u16;
    transplant u32 moved = amount + 1u32;
    transplant typedef u32 MovedType;
    transplant struct SplicedRecord { u32 value; } record = { 7u32 };
    transplant enum SplicedMode { spliced_mode_value = 3 } mode = spliced_mode_value;
    transplant declare_spliced!(macro_moved);
    transplant_inner inner_statement inner_moved;
    shadow_transplant relocated u32 relocated = amount;
    transplant_type typed_value u32;
    typed_value = 7u32;
    transplant_type pointer_value u32 *;
    pointer_value = &amount;
    transplant_type array_value u32 [2];
    array_value[0] = 3u32;
    array_value[1] = 4u32;
    transplant_type record_value struct SplicedTypeTag { u32 field; };
    record_value.field = 12u32;
    struct SplicedTypeTag *record_pointer = &record_value;
    typedef u16 CapturedAlias;
    shadow_type alias_value CapturedAlias;
    transplant_specifier base_value u16;
    base_value = 8u16;
    MovedType checked = moved;
    struct SplicedRecord later = { 9u32 };
    enum SplicedMode later_mode = spliced_mode_value;
    u32 __cross_syntax_splice = 2u32; // The marker spelling alone is ordinary source.
    // Structured grouping: (4 + 1) * 3; explicit projection: 4 + 1 * 3.
    if (multiplied (amount + 1u32) != 15u32 ||
        parse_splice (amount + 1u32) != 16u32 ||
        project_splice (amount + 1u32) != 8u32 ||
        inspect_splice (missing_macro! { 3u32 }) != 9u32 ||
        __cross_syntax_splice != 2u32 || checked != 5u32 ||
        record.value != 7u32 || later.value != 9u32 ||
        mode != spliced_mode_value || later_mode != spliced_mode_value ||
        macro_moved != 11u32 || inner_moved != 13u32 ||
        typed_value != 7u32 || *pointer_value != amount ||
        array_value[0] + array_value[1] != 7u32 ||
        record_pointer->field != 12u32 || base_value != 8u16 ||
        local_spliced_value != 17u32 || local_spliced_alias != 6u16 ||
        external_spliced_alias != 7u16 || named_by_macro != 5u32 ||
        *pointer_by_macro != amount || array_by_macro[1] != 3u32 ||
        copied_array[1] != 8u32 ||
        suffix_function(5u32) != 8u32 || parameter_function(5u32) != 7u32 ||
        header_function(5u32) != 9u32 || prototype_function(5u32) != 10u32 ||
        dispatch_empty(&empty_parameter_function) != 6u32 ||
        dispatch_empty(&void_parameter_function) != 7u32 ||
        list_parameter_function(3u32, 4u32) != 7u32 ||
        copied_function(5u32) != 11u32 ||
        deferred_copied_function(5u32) != 12u32 ||
        deferred_body_function(5u32) != 13u32 ||
        generic_header(5uptr) != 5uptr || recursive_header(5u32) != 120u32 ||
        sizeof(Source::Destination::moved) != 2uptr)
        return 0u32;
    return 61u32;
}
