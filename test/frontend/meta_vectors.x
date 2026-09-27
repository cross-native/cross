// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u32 u32x4 [[ext_vector_type(4)]];
typedef i32 mask4 [[ext_vector_type(4)]];
typedef f32 float4 [[ext_vector_type(4)]];
typedef i8 narrow4 [[ext_vector_type(4)]];
struct vector_box { u8 tag; u32x4 value; };
union lane_bits { f32 real; u32 word; };

[[eval_only]] static u32x4 changed_lane(in u32x4 value) {
    value[2u32] = 0xaabbccddu32;
    return value;
}

[[eval_only]] static $::meta::bytes copied_vector_bytes() {
    $::meta::buffer output = $::meta::alloc(32u32);
    u32 *words = (u32 *)$::meta::data(output);
    words[0u32] = 1u32;
    words[1u32] = 2u32;
    words[2u32] = 0x11223344u32;
    words[3u32] = 4u32;
    u32x4 *vectors = (u32x4 *)$::meta::data(output);
    u32x4 snapshot = changed_lane(vectors[0u32]);
    vectors[1u32] = snapshot;
    return $::meta::freeze(output, 32u32);
}

[[eval_only]] static uptr vector_record_ok() {
    $::meta::buffer source = $::meta::alloc(16u32);
    u32 *words = (u32 *)$::meta::data(source);
    words[0u32] = 1u32;
    words[1u32] = 2u32;
    words[2u32] = 3u32;
    words[3u32] = 4u32;
    $::meta::buffer destination = $::meta::alloc(sizeof(struct vector_box));
    struct vector_box *box = (struct vector_box *)$::meta::data(destination);
    box->value = changed_lane(((u32x4 *)$::meta::data(source))[0u32]);
    return box->value[0u32] == 1u32 &&
           box->value[2u32] == 0xaabbccddu32;
}

global const u8 vector_bytes[] = copied_vector_bytes();
global uptr vector_record_checked = vector_record_ok();

[[eval_only]] static u32x4 calculated_vector() {
    u32x4 value = 3u32;
    value = (value + 2u32) * (u32x4)4u32;
    value[1] = 8u32;
    value <<= 1u32;
    value ^= (u32x4)1u32;
    u32x4 inverted = ~value;
    mask4 mask = value > 20u32;
    mask4 zero = !value;
    float4 numbers = (float4)value / 2.0f32;
    mask4 float_mask = numbers >= 20.0f32;
    value[2] = mask[0] == -1i32 && mask[1] == 0i32 && zero[0] == 0i32 &&
        float_mask[0] == -1i32 && inverted[0] == 0xffffffd6u32 ? 99u32 : 0u32;
    return value;
}

global u32x4 evaluated_vector = calculated_vector();

[[eval_only]] static uptr vector_unary_ok(in u32x4 value) {
    narrow4 narrow = -2i8;
    mask4 complemented = ~narrow;
    mask4 promoted = +narrow;
    float4 positive_zero = 0.0f32;
    float4 negative_zero = -positive_zero;
    // Observe target floating bits through a union, not a numeric cast.
    union lane_bits lane = { .real = negative_zero[0] };
    return complemented[0] == 1i32 && promoted[3] == -2i32 &&
        lane.word == 0x80000000u32 && value[2] == 7u32;
}

global uptr vector_unary_checked = vector_unary_ok((u32x4)7u32);
$::static_assert(vector_unary_ok((u32x4)7u32), "required vector argument");

#ifndef META_COMPILE_ONLY
#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 meta_vector_entry() {
    u32x4 computed = $::eval(calculated_vector());
    return vector_record_checked == 1uptr &&
           vector_unary_checked == 1uptr &&
           evaluated_vector[0] == 41u32 && computed[1] == 17u32 && computed[2] == 99u32 &&
           vector_bytes[0u32] == 1u8 &&
           vector_bytes[8u32] == 0x44u8 &&
           vector_bytes[9u32] == 0x33u8 &&
           vector_bytes[24u32] == 0xddu8 &&
           vector_bytes[25u32] == 0xccu8 &&
           vector_bytes[31u32] == 0u8 ? 47u32 : 0u32;
}
#endif
