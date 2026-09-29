// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaPreparation {
    [[eval_only]] static T copy<T>(in T value) { return value; }
    [[eval_only]] static u32 forwarded() { return copy(7u32); }
    [[eval_only]] static bool constraint<uptr N>() {
        $::static_assert(copy(N) >= 3uptr, "nested generic helper assertion");
        return (bool)1u8;
    }
    enum Count [[underlying(uptr)]] { First = 3uptr, Elements };

    // Expansion preparation must not force unrelated declarations whose
    // ordinary semantic dependencies become available later in this input.
    static u32 late();
    enum Delayed [[underlying(u32)]] { Unneeded = late() };
    static T unused<T>(in T value) { return no_such_helper(value); }
    static u32 anchor;
    // Symbolic addresses may select instances, but are not emitted addresses
    // that an expander can inspect or dereference.
    [[eval_only]] static uptr address<u32 *P>() { return sizeof(P); }

    [[eval_only]] static uptr aggregate<uptr N>() {
        $::static_assert(constraint<N>(), "generic helper assertion");
        enum Bound [[underlying(uptr)]] { Length = N, Next };
        struct Row {
            uptr head;
            u16 values[(uptr)Length];
            u32 low : 3;
            u32 high : 5;
        } row = { .head = 11uptr, .values = {2u16, 3u16}, .low = 5u32, .high = 17u32 };
        struct Row saved = copy(row);
        if (saved.values[0uptr] != 2u16 || saved.values[1uptr] != 3u16 ||
            saved.values[N - 1uptr] != 0u16 || saved.low != 5u32 || saved.high != 17u32 ||
            sizeof(saved.values) != N * 2uptr || (uptr)Next != N + 1uptr ||
            sizeof(struct Row) < sizeof(uptr) + N * 2uptr ||
            $::alignof(struct Row) < $::alignof(uptr)) return 0uptr;
        return saved.head;
    }
    static $::meta::tokens prepare() {
        if (copy(7u32) != 7u32 || forwarded() != 7u32 || (uptr)Elements != 4uptr ||
            aggregate<(uptr)Elements>() != 11uptr || aggregate<3uptr>() != 11uptr ||
            address<&anchor>() != sizeof(u32 *) || $::meta::len(copy($::meta::parse("0u32"))) != 1uptr ||
            $::meta::len(copy($::meta::freeze(copy($::meta::alloc(2uptr)), 0uptr))) != 0uptr)
            return $::quote { 0u32 };
        return copy($::quote { 67u32 });
    }
    typedef u32 Word;
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        Word value = copy((Word)7u32);
        if (value != 7u32) return $::quote { 0u32 };
        return prepare();
    }
    [[noinline]] static u32 before_late() { return apply!(); }
    static u32 late() { return 19u32; }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        $::meta::context context = copy($::syntax::context(copy(input)));
        $::meta::syntax value = copy($::meta::parse("expr", $::quote { 67u32 }, context));
        return copy($::meta::tokens(value));
    }
    syntax Prepared : expression { prefix "prepared"; match "(" ")"; expand expand; }
    syntax Prepared;
    [[noinline]] static u32 run() {
        if (before_late() != 67u32 || apply!() != 67u32 || prepared() != 67u32 ||
            (u32)Unneeded != 19u32) return 0u32;
        return 67u32;
    }
}

namespace MetaPreparationImports {
    using MetaPreparation;
    [[macro]] static $::meta::tokens imported(in $::meta::tokens input) {
        Word value = copy(7u32);
        if (value != 7u32) return $::quote { 0u32 };
        return copy(input);
    }
    $::static_assert(imported!(1u32), "import context in expansion body");
}
