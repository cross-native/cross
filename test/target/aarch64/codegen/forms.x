// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Functions whose selected instructions, frames, and call sequences
// forms.cmake checks in the assembly.

struct triple {
    u32 a;
    u32 b;
    u32 c;
};

global u32 g_word = 7u32;
static u16 g_half = 9u16;

global u32 index_scaled(in u32 *p, in u64 i) { return p[i]; }
global u32 index_multiplied(in struct triple *p, in u64 i) { return p[i].b; }
global i8 load_signed_byte(in i8 *p) { return p[3]; }
global void store_half(in u16 *p, in u16 v) { p[-2] = v; }
global u32 unsigned_remainder(in u32 a, in u32 b) { return a % b; }
global i64 signed_remainder(in i64 a, in i64 b) { return a % b; }
global u64 wide_constant() { return 0x123456789abcdef0u64; }
global u64 inverted_constant() { return 0xffffffffffff1234u64; }
global u64 bitmask_constant() { return 0x00ff00ff00ff00ffu64; }
global bool compare_negative(in i32 a) { return a == -5; }
global u32 select_less(in u32 a, in u32 b) { return a < b ? a : b; }
global u32 shifts(in u32 a, in u32 n) { return (a << 3) ^ (a >> n); }
global i64 extend(in i16 a) { return a; }
global u32 load_globals() { return g_word + g_half; }
global void leaf() {}

[[abi("aapcs64"), noinline]]
global u64 c_ten(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e,
                 in u64 f, in u64 g, in u64 h, in u64 i, in u64 j) {
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j;
}

global u64 call_c_ten(in u64 x) {
    return c_ten(x, x, x, x, x, x, x, x, x + 1u64, x + 2u64);
}

[[noinline]]
global u32 x_eighteen(in u32 a1, in u32 a2, in u32 a3, in u32 a4, in u32 a5, in u32 a6,
                      in u32 a7, in u32 a8, in u32 a9, in u32 a10, in u32 a11, in u32 a12,
                      in u32 a13, in u32 a14, in u32 a15, in u32 a16, in u32 a17, in u32 a18) {
    return a1 + a17 + a18;
}

global u32 call_x_eighteen(in u32 x) {
    return x_eighteen(x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x + 1u32, x + 2u32);
}

global u32 call_pointer(in u32 (*f)(in u32 x), in u32 x) {
    return f(x) + 1u32;
}

global u32 tail_pointer(in u32 (*f)(in u32 x), in u32 x) {
    [[musttail]] return f(x);
}

[[noinline]] global u32 tail_target(in u32 x) { return x * 3u32; }

global u32 tail_direct(in u32 x) {
    [[musttail]] return tail_target(x + 1u32);
}

global u32 large_frame(in u32 i) {
    u8 data[70000];
    data[i] = 1u8;
    return data[i ^ 1u32];
}

[[noreturn]] global void spin();

[[noreturn, noinline]] global void never(in u32 x) {
    u32 table[4];
    table[x & 3u32] = x;
    spin();
}

struct block {
    u64 words[4];
};

// aapcs64 returns a record of more than 16 bytes to the address in x8 and
// passes one by reference.
[[abi("aapcs64"), noinline]]
global struct block c_block(in struct block b) {
    struct block result = b;
    result.words[0] = result.words[3];
    return result;
}

global u64 call_c_block(in u64 x) {
    struct block b;
    b.words[0] = x;
    b.words[3] = x + 1u64;
    struct block result = c_block(b);
    return result.words[0];
}

[[weak]] global u32 weak_function() { return 1u32; }
[[visibility("hidden")]] global u32 hidden_function() { return 2u32; }
