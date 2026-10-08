// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace VectorIndexErrors {
    typedef u32 U4 [[ext_vector_type(4)]];
    typedef uptr P16 [[vector_size(16)]];
    enum { Boundary = 4u32 };
    static U4 global_lanes;

#if MODE == 0
    static u32 unused(in U4 *lanes) { return (*lanes)[2u32 + 2u32]; }
#elif MODE == 1
    static void unused(in U4 *lanes) { (*lanes)[2u32 * 2u32] += 1u32; }
#elif MODE == 2
    static void unused(in U4 *lanes) { (*lanes)[1i32 - 2i32] = 0u32; }
#elif MODE == 3
    static uptr unused(in P16 *lanes, in uptr runtime_value) {
        return (*lanes)[16uptr / sizeof(runtime_value)];
    }
#elif MODE == 4
    static u32 unused(in U4 *lanes) {
        return (*lanes)[4uptr * $::alignof(U4) / $::alignof(U4)];
    }
#elif MODE == 5
    static u32 unused(in U4 *lanes) { return (*lanes)[1u128 << 80u32]; }
#elif MODE == 6
    static u32 unused(in U4 *lanes) { return (*lanes)[(u32)4.5f64]; }
#elif MODE == 7
    static u32 unused(in U4 *lanes) { return (*lanes)[VectorIndexErrors::Boundary]; }
#elif MODE == 8
    static u32 unused(in U4 *lanes) {
        return (*lanes)[1u32 ? 4u32 : (1u32 / 0u32)];
    }
#elif MODE == 9
    static u32 unused(in U4 *lanes) {
        return (*lanes)[(0u32 && (1u32 / 0u32)) + 4u32];
    }
#elif MODE == 10
    static uptr unused(in U4 *lanes) { return sizeof((*lanes)[2u32 + 2u32]); }
#elif MODE == 11
    static T generic<T, uptr N>(in T value) {
        T [[ext_vector_type(N)]] lanes = value;
        if ((bool)0u8) return lanes[N + 0uptr];
        return value;
    }
    [[eval_only]] static u32 use() { return generic<u32, 4uptr>(1u32); }
    static u32 result = use();
#elif MODE == 12
    [[macro]] static $::meta::tokens offset(in $::meta::tokens input) {
        return $::quote { 2u32 + 2u32 };
    }
    static u32 unused(in U4 *lanes) { return (*lanes)[offset!()]; }
#elif MODE == 13
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Copy : item { prefix "lane_function"; match body:function_def; expand copy; }
    syntax Copy;
    lane_function static void unused(in U4 *lanes) { ++(*lanes)[2u32 + 2u32]; }
#elif MODE == 14
    static $::meta::tokens unused(in $::meta::tokens input) {
        if ((bool)0u8) {
            U4 lanes = 0u32;
            lanes[2u32 + 2u32]--;
        }
        return input;
    }
#elif MODE == 15
    $::static_assert(sizeof(global_lanes[2u32 + 2u32]) == sizeof(u32), "lane type");
#elif MODE == 16
    struct Holder { u8 object[sizeof(global_lanes[Boundary])]; };
    static struct Holder object;
#elif MODE == 17
    enum { Width = sizeof(global_lanes[Boundary]) };
#elif MODE == 18
    static u32 unused(in U4 *lanes) { return (*lanes)[0u32 - 1u32]; }
#elif MODE == 19
    static u32 unused(in U4 *lanes) { return (*lanes)[(i8)(127u8 + 1u8)]; }
#elif MODE == 20
    static u32 unused(in U4 *lanes) { return *(&(*lanes)[2u32 + 2u32]); }
#elif MODE == 21
    [[macro]] static $::meta::tokens offset(in $::meta::tokens input) {
        return $::quote { Boundary };
    }
    static u32 unused(in U4 *lanes, in uptr Boundary) { return (*lanes)[offset!()]; }
#elif MODE == 22
    $::static_assert(1u32 ? 1u32 : global_lanes[2u32 + 2u32], "untaken lane");
#endif
}
global u32 entry() { return 1u32; }
