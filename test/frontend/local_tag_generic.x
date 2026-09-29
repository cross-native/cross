// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace LocalTagGeneric {
    [[noinline]] static T pass<T>(in T value) { return value; }
    [[noinline]] static bool same<T>(in T left, in T right) { return left == right; }
    [[noinline]] static T member<T>(in T input) {
        struct Local { T value; } object = {input};
        struct Local copied = pass(object);
        if (sizeof(copied) != sizeof(T)) return (T)0u32;
        return copied.value;
    }
    [[noinline]] static T recursive<T>(in T input) {
        struct Node;
        typedef struct Node NodeAlias;
        struct Node { T value; NodeAlias *next; } object;
        object.value = input;
        object.next = &object;
        struct Node copied = pass(object);
        if (!same(copied.next, &object)) return (T)0u32;
        return copied.next->value;
    }
    [[noinline]] static T union_value<T>(in T input) {
        union Local { T value; u8 byte; } object;
        object.value = input;
        union Local copied = pass(object);
        return copied.value;
    }
    static u32 increment<u32 N>() { return N + 1u32; }
    [[noinline]] static u32 enumeration<T, u32 N>(in T input) {
        enum Local [[underlying(u32)]] { A = N, B = A + (u32)sizeof(T), C = increment<(u32)A>() };
        enum Local item = pass(B);
        {
            enum Local [[underlying(u16)]] { A = 9u16, B = A + 2u16 } inner = B;
            if ((u16)pass(inner) != 11u16) return 0u32;
        }
        return (u32)item + (u32)C + (u32)input;
    }
    [[noinline]] static u32 bits<T, u32 N>(in T input) {
        enum Width [[underlying(u32)]] { WidthValue = N };
        struct Local { u32 value : (u32)WidthValue; T other; } object = {N - 1u32, input};
        struct Local copied = pass(object);
        return copied.value + (u32)copied.other;
    }
    [[noinline]] static T alignment<T, u32 N>(in T input) {
        struct Local [[aligned(N)]] { [[aligned(increment<N - 1u32>())]] T value; } object = {input};
        if ($::alignof(struct Local) != (uptr)N || sizeof(object) != (uptr)N) return (T)0u32;
        return object.value;
    }
    // Unused templates must not leak unsubstituted members into final layout.
    static T unused<T>(in T input) { struct Unused { T value; } object = {input}; return object.value; }

    [[noinline]] static u32 run(in u32 amount) {
        if (member(amount) != amount || member((u16)7u32) != 7u16 || member(amount + 1u32) != amount + 1u32 ||
            recursive(amount) != amount || recursive((u16)7u32) != 7u16 ||
            union_value(amount) != amount || union_value((u16)7u32) != 7u16 ||
            enumeration<u32, 3u32>(amount) != amount + 11u32 ||
            enumeration<u16, 5u32>(7u16) != 20u32 ||
            bits<u32, 4u32>(amount) != amount + 3u32 || bits<u16, 2u32>(7u16) != 8u32 ||
            alignment<u32, 8u32>(amount) != amount || alignment<u16, 4u32>(7u16) != 7u16)
            return 0u32;
        return amount;
    }
    $::static_assert(run(65549u32) == 65549u32, "generic-local nominal instances");
}
