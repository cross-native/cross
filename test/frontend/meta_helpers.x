// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaHelpers {
    typedef u32 Choice;
    [[noinline]] static u32 marker() { return 11u32; }
    static u32 ordinary(in u32 value) { return value + 1u32; }
    static void label_owner() { point: ; }
    static label label_value() { return label_owner::point; }
    static uptr label_layout(in $::meta::tokens input) {
        return sizeof(point) + $::alignof((point));
        point: ;
    }
    static $::meta::tokens label_helper() {
        if (label_layout($::quote {}) != sizeof(label) + $::alignof(label))
            return $::quote { 0u32 };
        label value = label_value();
        value = (label)value;
        label values[2] = {value, label_value()};
        $::meta::buffer storage = $::meta::alloc(2uptr * sizeof(label));
        label *stored = (label *)$::meta::data(storage);
        *stored = values[0];
        stored[1] = values[1];
        label numeric[2] = {};
        if (numeric[1] != (label)0uptr) return $::quote { 0u32 };
        numeric[1] = (label)0x3131uptr;
        $::meta::buffer numeric_storage = $::meta::alloc(sizeof(label));
        *((label *)$::meta::data(numeric_storage)) = numeric[1];
        if ((uptr)*((label *)$::meta::data(numeric_storage)) != 0x3131uptr)
            return $::quote { 0u32 };
        $::meta::bytes numeric_bytes = $::meta::freeze(numeric_storage, sizeof(label));
        u32 sum = 0u32;
        for (uptr index = 0uptr; index < sizeof(label); ++index) {
            sum += $::meta::at(numeric_bytes, index);
#if $::target::byte_order == $::target::order_little
            u8 expected = index < 2uptr ? 49u8 : 0u8;
#else
            u8 expected = index + 2uptr >= sizeof(label) ? 49u8 : 0u8;
#endif
            if ($::meta::at(numeric_bytes, index) != expected) return $::quote { 0u32 };
        }
        if (sum != 98u32) return $::quote { 0u32 };
        if (*stored == label_value() && stored[1] == value) return $::quote { 53u32 };
        return $::quote { 0u32 };
    }
    static $::meta::tokens literal() { return $::quote { marker() }; }
    static $::meta::tokens parsed() { return $::meta::parse("marker()"); }
    static $::meta::tokens alias() { return $::quote { sizeof(Choice) }; }
    static $::meta::buffer buffer(in const $::meta::buffer value) { return value; }
    static $::meta::bytes bytes(in const $::meta::bytes value) { return value; }
    static $::meta::tokens byte_helper() {
        $::meta::buffer allocated = $::meta::alloc(4uptr);
        u32 *data = (u32 *)$::meta::data(buffer(allocated));
        *data = 0x31313131u32;
        $::meta::bytes value = bytes($::meta::freeze(allocated, 4uptr));
        if ($::meta::len(value) != 4uptr || $::meta::at(value, 0uptr) != 49u8)
            return $::quote { 0u32 };
        return $::quote { 51u32 };
    }
    static $::meta::tokens at_caller() {
        return $::quote { $::unquote($::meta::call_site($::meta::parse("marker")))() };
    }
    static $::meta::syntax_match match(in const $::meta::syntax_match value) { return value; }
    static $::meta::context context(in const $::meta::context left, in const $::meta::context right, in bool first) {
        return first ? left : right;
    }
    static $::meta::span span(in const $::meta::syntax value) { return $::meta::node_span(value); }
    static void check(in const $::meta::syntax value, in const $::meta::span where) {
        if (!$::meta::is_production(value, "assignment_expression"))
            $::syntax::error(where, "helper received the wrong root");
    }
    static $::meta::syntax transform(in const $::meta::syntax value, in const $::meta::syntax replacement) {
        if ($::meta::is_production(value, "literal")) return replacement;
        $::meta::syntax result = value;
        if ($::meta::is_kind(result, "core") || $::meta::is_kind(result, "group"))
            for (uptr i = 0uptr; i < $::meta::child_count(result); ++i)
                result = $::meta::replace_child(result, i, transform($::meta::child(result, i), replacement));
        return result;
    }
    static $::meta::tokens rewrite(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(match(input), "value");
        $::meta::context source = $::syntax::context(input);
        $::meta::syntax replacement = $::meta::parse("expr", $::quote { 7u32 }, context(source, source, (bool)1u8));
        check(value, span(value));
        while (!$::meta::is_production(replacement, "literal")) replacement = $::meta::child(replacement, 0uptr);
        if (ordinary(6u32) != 7u32) $::syntax::error(span(value), "ordinary helper call failed");
        return $::quote { $::unquote(transform(value, replacement)) };
    }
    static $::meta::tokens fresh() { return $::meta::gensym("local"); }
    static $::meta::tokens token_helper(in $::meta::tokens value) {
        $::meta::tokens first = fresh();
        $::meta::tokens second = fresh();
        return $::quote { {
            u32 $::unquote(first) = $::unquote(value);
            u32 $::unquote(second) = $::unquote(first) + 1u32;
            return $::unquote(second);
        } };
    }
}
namespace MetaHelperBridge {
    typedef u64 Choice;
    [[noinline]] static u32 marker() { return 19u32; }
    static $::meta::tokens nested(in $::meta::tokens input) {
        $::meta::tokens before = $::quote { marker() };
        $::meta::tokens inner_literal = MetaHelpers::literal();
        $::meta::tokens after = $::meta::parse("marker()");
        $::meta::tokens inner_parse = MetaHelpers::parsed();
        $::meta::tokens caller = MetaHelpers::at_caller();
        $::meta::tokens inner_alias = MetaHelpers::alias();
        // Returning from a helper restores this helper's definition context.
        // Copied input and explicit call_site still use the original invocation.
        return $::quote { ($::unquote(before) + $::unquote(inner_literal) +
            $::unquote(after) + $::unquote(inner_parse) + $::unquote(caller) +
            (u32)$::unquote(inner_alias) + (u32)sizeof(Choice) + $::unquote(input)) };
    }
}
namespace MetaHelperOwners {
    typedef u8 Choice;
    static u32 marker() { return 13u32; }
    [[syntax_expander]] static $::meta::tokens rewrite(in $::meta::syntax_match input) {
        return MetaHelpers::rewrite(input);
    }
    syntax Rewrite : expression { prefix "rewrite_tree"; match "(" value:expr ")"; expand rewrite; }
    [[macro]] static $::meta::tokens literal(in $::meta::tokens input) { return MetaHelpers::literal(); }
    [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) { return MetaHelpers::parsed(); }
    [[macro]] static $::meta::tokens alias(in $::meta::tokens input) { return MetaHelpers::alias(); }
    [[macro]] static $::meta::tokens byte_helper(in $::meta::tokens input) { return MetaHelpers::byte_helper(); }
    [[macro]] static $::meta::tokens label_helper(in $::meta::tokens input) { return MetaHelpers::label_helper(); }
    [[macro]] static $::meta::tokens at_caller(in $::meta::tokens input) { return MetaHelpers::at_caller(); }
    [[macro]] static $::meta::tokens token_helper(in $::meta::tokens input) { return MetaHelpers::token_helper(input); }
    [[macro]] static $::meta::tokens nested(in $::meta::tokens input) { return MetaHelperBridge::nested(input); }
}
namespace MetaHelperOtherCaller {
    typedef u8 Choice;
    [[noinline]] static u32 marker() { return 29u32; }
    [[noinline]] static u32 run() { return MetaHelperOwners::nested!(marker()); }
}
namespace MetaHelperCaller {
    typedef u16 Choice;
    [[noinline]] static u32 marker() { return 17u32; }
    [[noinline]] static u32 token_result() { MetaHelperOwners::token_helper!(22u32) }
    [[noinline]] static u32 run() {
        syntax MetaHelperOwners::Rewrite;
        if (rewrite_tree(2u32 + 3u32 * 4u32) != 56u32 ||
            MetaHelperOwners::literal!() != 11u32 ||
            MetaHelperOwners::parsed!() != 11u32 ||
            MetaHelperOwners::alias!() != 4uptr ||
            MetaHelperOwners::byte_helper!() != 51u32 ||
            MetaHelperOwners::label_helper!() != 53u32 ||
            MetaHelperOwners::nested!(marker()) != 106u32 ||
            MetaHelperOtherCaller::run() != 130u32 ||
            MetaHelperOwners::at_caller!() != 17u32 || token_result() != 23u32) return 0u32;
        return 61u32;
    }
}
