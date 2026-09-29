// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace LocalTagScope {
    struct Local { u8 value; };
    [[noinline]] static T pass<T>(in T value) { return value; }
    [[noinline]] static u32 first(in u32 amount) {
        struct Local { u16 value; } object = {(u16)amount};
        enum E [[underlying(u16)]] { A = 3u16, B = A + 2u16 } item = B;
        enum Future [[underlying(u16)]];
        enum Future future = 3u16;
        enum Future [[underlying(u16)]] { FutureValue = 5u16 };
        if (sizeof(future) != 2uptr || (u32)FutureValue != 5u32) return 0u32;
        {
            struct Local { u32 value; } inner = {65549u32};
            if (sizeof(inner) != 4uptr || inner.value != 65549u32) return 0u32;
            enum E [[underlying(u32)]] { A = 5u32, B = A + 2u32 } inner_enum = B;
            if (sizeof(inner_enum) != 4uptr || (u32)inner_enum != 7u32) return 0u32;
        }
        struct Local copied = pass(object);
        return (u32)copied.value + (u32)item + (u32)sizeof(object);
    }
    [[noinline]] static u32 second(in u32 amount) {
        struct Local;
        struct Local *pointer;
        struct Local { u32 value; } object = {amount};
        pointer = &object;
        enum E [[underlying(u32)]] { A = 5u32, B = A + 2u32 } item = B;
        return pointer->value + (u32)item + (u32)sizeof(object);
    }
    [[noinline]] static u32 loops() {
        u32 result = 0u32;
        for (struct Local { u32 value; } item = {2u32}; item.value != 0u32; --item.value) {
            result += item.value;
            if (sizeof(struct Local) != 4uptr) return 0u32;
        }
        if (sizeof(struct Local) != 1uptr) return 0u32;
        return result;
    }
    [[noinline]] static u32 unions(in u32 amount) {
        union Local { u32 word; u16 half; } object;
        typedef union Local Alias;
        object.word = amount;
        {
            union Local { u16 half; } inner = {7u16};
            if (sizeof(inner) != 2uptr || inner.half != 7u16) return 0u32;
        }
        Alias copy = object;
        return copy.word;
    }
    $::static_assert(first(11u32) == 18u32, "first local tag");
    $::static_assert(second(65549u32) == 65560u32, "second local tag");
    $::static_assert(loops() == 3u32, "for tag scope");
    $::static_assert(unions(65549u32) == 65549u32, "union tag scope");
    $::static_assert(sizeof(struct Local) == 1uptr, "namespace tag unchanged");
}
