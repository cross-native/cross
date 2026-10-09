// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls through function pointers of the default and an explicit ABI, with
// results, cells, and a pointer held in a static object.

typedef u32 (*unary)(in u32 value);
typedef void (*accumulate)(inout u32 total, in u32 value);
typedef u64 (*sysv_binary)(in u64 left, in u64 right) [[abi("sysv_abi")]];

[[noinline]] static u32 twice(in u32 value) { return value * 2u32; }
[[noinline]] static u32 square(in u32 value) { return value * value; }
[[noinline]] static void add_into(inout u32 total, in u32 value) {
    total += value;
}
[[abi("sysv_abi"), noinline]] static u64 sysv_add(in u64 left, in u64 right) {
    return left + right;
}

global unary chosen = square;

[[noinline]] static u32 apply(in unary f, in u32 value) { return f(value); }

[[abi("ms_abi"), link_name("gimple_indirect_entry"), noinline]]
global u32 gimple_indirect_entry(in u32 seed) {
    unary f = square;
    if ((seed & 1u32) != 0u32) f = twice;
    u32 total = apply(f, seed) + apply(twice, 1u32) + chosen(3u32);
    accumulate add = add_into;
    add(total, 100u32);
    sysv_binary s = sysv_add;
    return (u32)(s(1u64 << 40u32, (u64)total) - (1u64 << 40u32));
}
