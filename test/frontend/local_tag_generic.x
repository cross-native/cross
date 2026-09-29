// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace LocalTagGeneric {
    typedef struct { u16 value; } AnonymousRecord;
    typedef enum [[underlying(u16)]] { AnonymousA = 5u16, AnonymousB } AnonymousEnum;
    enum [[underlying(u32)]] { AnonymousGlobal = 11u32 };
    $::static_assert(sizeof(AnonymousRecord) == 2uptr && sizeof(AnonymousEnum) == 2uptr &&
        (u16)AnonymousB == 6u16 && (u32)AnonymousGlobal == 11u32, "file-scope anonymous tags");
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
    static u32 layout_helper<u32 N>() {
        struct Bits { u32 value : 4; u32 next : 4; } object = {N, 1u32};
        enum Size [[underlying(uptr)]] { Bytes = sizeof(struct Bits) };
        if ((uptr)Bytes != 4uptr) return 0u32;
        return object.value + object.next;
    }
    // A global enum can instantiate a helper that publishes its own local enum
    // and record while the outer enum is still being evaluated.
    enum Helper [[underlying(u32)]] { HelperValue = layout_helper<7u32>(), HelperNext };
    $::static_assert((u32)HelperValue == 8u32 && (u32)HelperNext == 9u32, "early generic enum call");
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
    [[noinline]] static T arrays<T, u32 N>(in T input) {
        enum Count [[underlying(u32)]] { CountValue = N };
        struct Cell { T value; };
        struct Local {
            T grid[(u32)CountValue][increment<N>()];
            u8 bytes[sizeof(struct Cell)];
            T (*pointer)[N + 1u32];
        } object = { { { (T)0u32 } } };
        object.grid[N - 1u32][N] = input;
        object.bytes[sizeof(T) - 1uptr] = 7u8;
        object.pointer = pass(&object.grid[N - 1u32]);
        if (object.bytes[sizeof(T) - 1uptr] != 7u8) return (T)0u32;
        struct Local copied = object;
        struct Small { T data[2]; } small = {{input, (T)0u32}};
        struct Small transported = pass(small);
        if (transported.data[0] != input) return (T)0u32;
        return (*copied.pointer)[N];
    }
    [[noinline]] static T early_layout<T, u32 N>(in T input) {
        enum Count [[underlying(u32)]] { CountValue = N };
        struct Cell { T value; };
        struct Local [[aligned(layout_helper<7u32>())]] {
            T values[layout_helper<(u32)CountValue>()];
            u8 bytes[sizeof(struct Cell *)];
            T grid[2][sizeof(struct Cell)];
        };
        enum Layout [[underlying(uptr)]] {
            Alignment = $::alignof(struct Local),
            Bytes = sizeof(struct Local),
            CellBytes = sizeof(struct Cell),
            PointerBytes = sizeof(struct Cell *)
        };
        if ((uptr)Alignment != 8uptr || (uptr)Bytes % 8uptr != 0uptr ||
            (uptr)CellBytes != sizeof(T) || (uptr)PointerBytes != sizeof(uptr)) return (T)0u32;
        struct Local object = {{(T)0u32}};
        object.values[N] = input;
        object.grid[1][sizeof(T) - 1uptr] = input;
        if ((*pass(&object.grid[1]))[sizeof(T) - 1uptr] != input) return (T)0u32;
        return object.values[N];
    }
    [[noinline]] static T anonymous<T, u32 N>(in T input) {
        struct { T value; } first = {input}, second = pass(first);
        if (second.value != input) return (T)0u32;
        typedef struct { T value; u8 bytes[N]; } Local;
        Local object = {second.value}, copied = object;
        typedef union [[aligned(8)]] { T value; u8 byte; } Variant;
        Variant variant;
        variant.value = copied.value;
        if (sizeof(Variant) != 8uptr) return (T)0u32;
        enum [[underlying(u32)]] { Count = N, Next = Count + 1u32 };
        typedef enum [[underlying(u16)]] { A = (u16)N, B } Flag;
        Flag item = pass(B);
        {
            enum [[underlying(u32)]] { Count = 9u32 };
            if ((u32)pass(Count) != 9u32) return (T)0u32;
        }
        if ((u32)pass(Count) != N || (u32)Next != N + 1u32 || (u16)item != (u16)(N + 1u32)) return (T)0u32;
        AnonymousRecord outer = {7u16};
        AnonymousRecord outer_copy = pass(outer);
        AnonymousEnum outer_enum = pass(AnonymousB);
        if (outer_copy.value != 7u16 || (u16)outer_enum != 6u16) return (T)0u32;
        return variant.value;
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
            alignment<u32, 8u32>(amount) != amount || alignment<u16, 4u32>(7u16) != 7u16 ||
            arrays<u32, 3u32>(amount) != amount || arrays<u16, 2u32>(7u16) != 7u16 ||
            early_layout<u32, 3u32>(amount) != amount || early_layout<u16, 2u32>(7u16) != 7u16 ||
            anonymous<u32, 3u32>(amount) != amount || anonymous<u16, 2u32>(7u16) != 7u16)
            return 0u32;
        return amount;
    }
    $::static_assert(run(65549u32) == 65549u32, "generic-local nominal instances");
}
