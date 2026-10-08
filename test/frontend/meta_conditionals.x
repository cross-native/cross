// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaChoices {
    static T copy<T>(in T value) { return value; }
    static T choose<T>(in bool first, in const T left, in T right) {
        return first ? left : right;
    }
    static $::meta::buffer unselected() {
        u32 invalid = 1u32 / 0u32;
        return $::meta::alloc((uptr)invalid);
    }
    [[macro]] static $::meta::tokens bytes(in $::meta::tokens input) {
        bool first = $::meta::len(input) != 0uptr;
        $::meta::buffer left = $::meta::alloc(2uptr);
        $::meta::buffer right = $::meta::alloc(3uptr);
        const $::meta::buffer readonly = left;
        $::meta::buffer selected = first ? readonly : right;
        $::meta::buffer inferred = copy(first ? readonly : right);
        $::meta::buffer helper = choose(first, readonly, right);
        $::meta::buffer shorted = first ? inferred : unselected();
        $::meta::data(selected)[0uptr] = 7u8;
        $::meta::data(shorted)[1uptr] = 9u8;
        if ($::meta::cap(helper) != 2uptr || $::meta::data(helper)[0uptr] != 7u8 ||
            $::meta::data(left)[1uptr] != 9u8) return $::quote { 0u32 };
        const $::meta::bytes full = $::meta::freeze(left, 2uptr);
        $::meta::bytes empty = $::meta::freeze(right, 0uptr);
        $::meta::bytes value = first ? full : empty;
        value = choose(first, full, empty);
        $::meta::bytes inferred_bytes = copy(first ? full : empty);
        // The unselected freeze would fail: right was already consumed.
        $::meta::bytes shorted_bytes = first ? value : $::meta::freeze(right, 0uptr);
        if ($::meta::len(inferred_bytes) != 2uptr || $::meta::at(shorted_bytes, 1uptr) != 9u8)
            return $::quote { 0u32 };
        return first ? input : $::quote { 0u32 };
    }

    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        const $::meta::syntax_match readonly = input;
        $::meta::syntax_match selected = copy((bool)1u8 ? readonly : input);
        selected = choose((bool)0u8, readonly, input);
        const $::meta::syntax node = $::syntax::node(selected, "value");
        $::meta::syntax other = $::syntax::node(input, "value");
        $::meta::syntax result = copy((bool)1u8 ? node : other);
        result = choose((bool)0u8, node, other);
        const $::meta::context original = $::syntax::context(node);
        $::meta::context alternate = $::syntax::context(other);
        $::meta::context context = copy((bool)1u8 ? original : alternate);
        context = choose((bool)0u8, original, alternate);
        const $::meta::span where = $::meta::node_span(node);
        $::meta::span elsewhere = $::meta::node_span(other);
        $::meta::span location = copy((bool)1u8 ? where : elsewhere);
        location = choose((bool)0u8, where, elsewhere);
        const $::meta::tokens literal = $::quote { 79u32 };
        $::meta::tokens alternative = $::quote { 0u32 };
        $::meta::tokens tokens = copy((bool)1u8 ? literal : alternative);
        tokens = choose((bool)1u8, literal, alternative);
        $::meta::syntax parsed = $::meta::parse("expr", tokens, context);
        if (!$::meta::is_production(parsed, "assignment_expression") ||
            !$::meta::is_production(result, "assignment_expression"))
            $::syntax::error(location, "conditional meta value lost its kind or context");
        return $::quote { $::unquote(result) };
    }
    syntax Select : expression { prefix "meta_select"; match "(" value:expr ")"; expand expand; }
    syntax Select;

    // Byte conditionals are also valid in mandatory evaluation without an
    // enclosing procedural expansion or syntax invocation.
    static $::meta::bytes initialize(in bool first) {
        $::meta::buffer storage = $::meta::alloc(1uptr);
        $::meta::data(storage)[0uptr] = 79u8;
        const $::meta::bytes full = $::meta::freeze(storage, 1uptr);
        $::meta::bytes empty = $::meta::freeze($::meta::alloc(0uptr), 0uptr);
        return first ? full : empty;
    }
    static const u8 materialized[] = initialize((bool)1u8);
    static uptr pointer_width<const u8 *P>() { return sizeof(P); }
    [[noinline]] static u32 run() {
        if (bytes!(79u32) != 79u32 || meta_select(79u32) != 79u32 ||
            materialized[0uptr] != 79u8 || pointer_width<materialized>() != sizeof(uptr)) return 0u32;
        return 79u32;
    }
}
