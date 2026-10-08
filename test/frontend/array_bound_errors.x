// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
namespace ArrayBoundErrors {
    static uptr zero() { return 0uptr; }
    static uptr four() { return 4uptr; }
#if MODE == 0
    static u8 object[zero()];
#elif MODE == 1
    typedef u8 Unused[zero()];
#elif MODE == 2
    static u32 unused(in u8 value[zero()]) { return 0u32; }
#elif MODE == 3
    typedef u32 (*Unused)(in u8 value[zero()]);
#elif MODE == 4
    static u32 adjusted<uptr N>(in u8 value[N]) { return 1u32; }
    [[eval_only]] static u32 use() {
        u8 value[1] = { 1u8 };
        return adjusted<0uptr>(value);
    }
    static u32 value = use();
#elif MODE == 5
    static u32 unused(in uptr runtime_size) {
        static u8 storage[runtime_size];
        return 1u32;
    }
#elif MODE == 6
    static i32 negative() { return -1i32; }
    static u8 object[negative()];
#elif MODE == 7
    typedef u8 Alias[four()];
    typedef u8 Alias[four() + 1uptr];
#elif MODE == 8
    static u64 huge() { return 4294967296u64; }
    static u8 object[huge()];
#elif MODE == 9
    typedef u32 U4 [[ext_vector_type(4)]];
    static U4 lanes;
    static u8 object[sizeof(lanes[2u32 + 2u32])];
#elif MODE == 10
    static $::meta::tokens unused(in $::meta::tokens input) {
        typedef u8 Unused[zero()];
        return input;
    }
#elif MODE == 11
    static f32 floating() { return 4.0f32; }
    static u8 object[floating()];
#elif MODE == 12
    static u8 object[four() - 1uptr] = "abc";
#elif MODE == 13
    static u8 object[four()] = { [4uptr] = 1u8 };
#elif MODE == 14
    static u32 unused(in u8 (*row)[zero()]) { return 0u32; }
#elif MODE == 15
    static u32 unused() {
        typedef u8 Unavailable[sizeof(later)];
        u32 later = 4u32;
        return 0u32;
    }
#elif MODE == 16
    static u32 unused() {
        { u32 hidden = 4u32; }
        typedef u8 Unavailable[sizeof(hidden)];
        return 0u32;
    }
#elif MODE == 17
    static u32 unused(in uptr value) {
        typedef u8 Write[(value = 4uptr)];
        return 0u32;
    }
#elif MODE == 18
    static uptr write(in uptr *value) { *value = 4uptr; return *value; }
    static u32 unused(in uptr value) {
        typedef u8 Write[write(&value)];
        return 0u32;
    }
#elif MODE == 19
    typedef u8 Zero[0xffffffffu32 + 1u32];
#elif MODE == 20
    typedef u8 Negative[0i32 - 1i32];
#elif MODE == 21
    typedef u8 Excess[(1u128 << 96u32) + 1u128];
#elif MODE == 22
    typedef u8 InvalidLiteral[256u8];
#elif MODE == 23
    static u32 unused() {
        struct Unavailable { u8 bytes[sizeof(later)]; };
        u32 later = 4u32;
        return 0u32;
    }
#elif MODE == 24
    static u32 unused(in uptr value) {
        struct Unavailable { u8 bytes[value]; };
        return 0u32;
    }
#elif MODE == 25
    struct Unavailable { u8 bytes[sizeof(value)]; };
    static u32 unrelated(in uptr value) { return (u32)sizeof(struct Unavailable); }
#elif MODE == 26
    static u32 unused() {
        { u32 hidden = 4u32; }
        struct Unavailable { u8 bytes[sizeof(hidden)]; };
        return 0u32;
    }
#endif
}
global u32 entry() { return 1u32; }
