// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static $::meta::syntax replace_attributes(in $::meta::syntax node, in $::meta::syntax attribute) {
    if ($::meta::is_production(node, "attribute_specifier")) return attribute;
    for (uptr index = 0uptr; index < $::meta::child_count(node); ++index) {
        $::meta::syntax changed = replace_attributes($::meta::child(node, index), attribute);
        node = $::meta::replace_child(node, index, changed);
    }
    return node;
}
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::static_assert($::meta::is_production(node, "type_name"), "attribute changed root");
    $::meta::syntax copied = $::meta::parse("type", $::meta::tokens(node), $::syntax::context(node));
    $::static_assert($::meta::is_production(copied, "type_name"), "attribute projection failed");
    return $::quote { 1u32 };
}
[[syntax_expander]] static $::meta::tokens repair(in $::meta::syntax_match input) {
    $::meta::syntax valid = $::meta::parse("type", $::quote { u32 [[ext_vector_type(4)]] }, $::syntax::context(input));
    $::meta::syntax attribute = $::meta::child($::meta::child($::meta::child(valid, 0uptr), 1uptr), 0uptr);
    $::meta::syntax fixed = replace_attributes($::syntax::node(input, "value"), attribute);
    return $::quote { sizeof($::unquote(fixed)) };
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
[[syntax_expander]] static $::meta::tokens repair_function(in $::meta::syntax_match input) {
    $::meta::syntax valid = $::meta::parse("type", $::quote { u32 [[ext_vector_type(4)]] }, $::syntax::context(input));
    $::meta::syntax attribute = $::meta::child($::meta::child($::meta::child(valid, 0uptr), 1uptr), 0uptr);
    $::meta::syntax fixed = replace_attributes($::syntax::node(input, "value"), attribute);
    return $::quote { $::unquote(fixed) };
}
[[syntax_expander]] static $::meta::tokens repair_enum(in $::meta::syntax_match input) {
    $::meta::syntax valid = $::meta::parse("type", $::quote { enum E [[underlying(u16)]] { value = 0 } }, $::syntax::context(input));
    $::meta::syntax specifier = $::meta::child($::meta::child($::meta::child($::meta::child(valid, 0uptr), 0uptr), 0uptr), 0uptr);
    $::meta::syntax fixed = replace_attributes($::syntax::node(input, "value"), $::meta::child(specifier, 2uptr));
    return $::quote { $::unquote(fixed) };
}
syntax Inspect : expression { prefix "inspect"; match "(" value:type ")"; expand inspect; }
syntax Repair : expression { prefix "repair"; match "(" value:type ")"; expand repair; }
syntax Copy : item { prefix "copy"; match value:function_def; expand copy; }
syntax RepairFunction : item { prefix "repair_function"; match value:function_def; expand repair_function; }
syntax RepairEnum : item { prefix "repair_enum"; match value:function_def; expand repair_enum; }
syntax Inspect, Repair, Copy, RepairFunction, RepairEnum;
repair_function static uptr repaired_alias() {
    typedef u16 Pack [[ext_vector_type(0)]];
    return sizeof(Pack);
}
repair_enum static uptr repaired_enum() {
    enum E [[underlying(f64)]] { value = 0 };
    typedef enum E Alias;
    return sizeof(Alias);
}
copy [[noinline]] static uptr represented() {
    typedef uptr Pair [[vector_size(sizeof(uptr) * 2uptr)]];
    return sizeof(Pair);
}
copy [[noinline]] static uptr generic<T>() {
    typedef T Pack [[ext_vector_type(4)]];
    return sizeof(Pack);
}
#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (inspect([[vendor::annotation({ [first] })]] u32 *) != 1u32 ||
        inspect(u32 * [[vendor::annotation]] const) != 1u32 ||
        inspect(u32 [[atomic(1), atomic, atomic]]) != 1u32 ||
        inspect(u32 [[address_space(-1)]]) != 1u32 ||
        inspect(u32 [[vector_size(3)]]) != 1u32 ||
        inspect(bool [[ext_vector_type(4)]]) != 1u32 ||
        inspect(u32 (*)(in u16) [[abi(1), clobber(), stack_cleanup(7)]]) != 1u32 ||
        inspect(enum E [[underlying(f64)]] { value = 0 }) != 1u32)
        return 1u32;
    if (repair(u32 [[ext_vector_type(+)]]) != 16uptr ||
        repair(u8 [[vector_size({ [invalid] })]]) != 4uptr ||
        repair([[vendor::annotation]] u16) != 8uptr)
        return 2u32;
    if (represented() != 2uptr * sizeof(uptr) || generic<u16>() != 8uptr ||
        repaired_alias() != 8uptr || repaired_enum() != 2uptr)
        return 3u32;
    return 61u32;
}
