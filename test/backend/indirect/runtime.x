// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u32 (*callback)(in u32 a, in u32 b, in u32 c, in u32 d,
    in u32 e, in u32 f, in u32 g, in u32 h, in u32 i, in u32 j);

[[noinline]] u32 add(in u32 a, in u32 b, in u32 c, in u32 d,
    in u32 e, in u32 f, in u32 g, in u32 h, in u32 i, in u32 j) {
    return a + b * 3u32 + c * 5u32 + d * 7u32 + e * 11u32 +
        f * 13u32 + g * 17u32 + h * 19u32 + i * 23u32 + j * 29u32;
}
[[noinline]] u32 mix(in u32 a, in u32 b, in u32 c, in u32 d,
    in u32 e, in u32 f, in u32 g, in u32 h, in u32 i, in u32 j) {
    return (a ^ b) + (c ^ d) + (e ^ f) + (g ^ h) + (i ^ j);
}
[[noinline]] u32 invoke(in callback cb, in u32 x) {
    u32 a = x + 11u32;
    u32 b = x ^ 123u32;
    u32 c = x * 17u32;
    u32 result = (*cb)(x, 2u32, 3u32, 4u32, 5u32, 6u32,
        7u32, 8u32, 9u32, 10u32);
    return result + a + b + c;
}
callback saved = &add;
[[noinline]] callback choose(in callback choices[2], in u32 index) {
    return choices[index];
}
global u32 indirect_test(in u32 x) {
    callback choices[2];
    choices[0] = saved;
    choices[1] = mix;
    callback cb = choose(choices, x & 1u32);
    return invoke(cb, x);
}

global u32 indirect_external(in callback cb, in u32 x) {
    return invoke(cb, x);
}

typedef void (*mutator)(out u32 a, inout u32 b);
[[noinline]] void mutate(out u32 a, inout u32 b) {
    a = b + 7u32;
    b = b * 3u32;
}
[[noinline]] u32 apply_mutator(in mutator cb) {
    u32 a = 0u32;
    u32 b = 11u32;
    cb(a, b);
    return a + b;
}
global u32 indirect_modes() {
    return apply_mutator(mutate);
}

typedef u64 (*wide_callback)(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e);
[[noinline]] u64 wide_add(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e) {
    return (a ^ b) + c + d + e;
}
[[noinline]] u64 apply_wide(in wide_callback cb, in u64 value) {
    u64 result = cb(value, 0xfedcba9876543210u64, 0x100000001u64,
        0x700000009u64, 0x200000003u64);
    return result ^ value;
}
global u32 indirect_wide() {
    u64 value = 0x123456789abcdef0u64;
    u64 expected = ((value ^ 0xfedcba9876543210u64) + 0x100000001u64 +
        0x700000009u64 + 0x200000003u64) ^ value;
    return apply_wide(wide_add, value) == expected;
}
