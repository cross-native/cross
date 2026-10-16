// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A volatile parameter is a volatile callee cell: copy-in and copy-out are
// volatile accesses, and the ABI is that of the unqualified type. Deduction
// from a volatile lvalue binds the unqualified type. test_entry returns zero
// when every result matches; it runs with -fno-eval-calls, so that the calls
// run.

[[noinline]] static u32 widen(in volatile u16 value) {
    return (u32)value + (u32)value;
}

[[noinline]] static void scale(inout volatile i32 value, in i32 factor) {
    value = value * factor;
}

[[noinline]] static void produce(out volatile u64 value) {
    value = 0x123456789abcdef0u64;
}

[[noinline]] global f64 halve(in volatile f64 value) {
    value = value / 2.0;
    return value;
}

static u32 count<T>(in T value) {
    T copy = value;
    copy = copy + (T)1;
    return (u32)copy;
}

global volatile u16 sixteen = 0x8001u16;

global u32 test_entry() {
    if ($::runtime(widen(0x7001u16)) != 0xe002u32) return 1u32;
    i32 scaled = -7;
    $::runtime(scale(scaled, 6));
    if (scaled != -42) return 2u32;
    u64 produced = 0u64;
    $::runtime(produce(produced));
    if (produced != 0x123456789abcdef0u64) return 3u32;
    if ($::runtime(halve(5.0)) != 2.5) return 4u32;
    if (count(sixteen) != 0x8002u32) return 5u32;
    return 0u32;
}
