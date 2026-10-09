// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Mandatory tail transfers with `out` and `inout` parameters. An output
// passed on as an output argument is forwarded through the caller's own
// transport pointer, so the tail callee delivers it; every other output is
// delivered before the transfer.

[[abi("sysv_abi"), noinline]]
global void musttail_out_callee(out i32 x) { x = 7; }

[[abi("sysv_abi"), noinline]]
global void musttail_out_caller(out i32 x) {
    [[musttail]] return musttail_out_callee(x);
}

[[noinline]]
global void musttail_triple(inout i32 x) { x = x * 3; }

[[noinline]]
global void musttail_bump_then_triple(inout i32 x) {
    x = x + 1;
    [[musttail]] return musttail_triple(x);
}

[[noinline]]
global void musttail_assign_then_triple(out i32 x) {
    x = 6;
    [[musttail]] return musttail_triple(x);
}

[[noinline]]
global i32 musttail_produce(out i32 a, in i32 v) {
    a = v + 7;
    return v - 1;
}

[[noinline]]
global i32 musttail_mixed(out i32 a, out i32 b, in i32 v) {
    b = v * 10;
    [[musttail]] return musttail_produce(a, v);
}

struct musttail_pair { u64 low; u64 high; };

[[noinline]]
global void musttail_fill(out struct musttail_pair pair, in u64 v) {
    pair.low = v;
    pair.high = v + 1u64;
}

[[noinline]]
global void musttail_fill_twice(out struct musttail_pair pair, in u64 v) {
    [[musttail]] return musttail_fill(pair, v * 2u64);
}

[[noinline]]
global void musttail_half(out f64 d, in i32 v) { d = (f64)v * 0.5; }

[[noinline]]
global void musttail_half_sum(out f64 d, in i32 v, in i32 w) {
    [[musttail]] return musttail_half(d, v + w);
}

// A may_alias spelling of the same type forwards like the type itself.
typedef i32 musttail_any [[may_alias]];

[[abi("sysv_abi"), noinline]]
global void musttail_aliased_out(out musttail_any x) {
    [[musttail]] return musttail_out_callee(x);
}

global i32 musttail_outputs_entry() {
    i32 seed = $::runtime(4);
    i32 x = 0;
    musttail_out_caller(x);
    i32 y = seed;
    musttail_bump_then_triple(y);
    i32 z = 0;
    musttail_assign_then_triple(z);
    i32 a = 0;
    i32 b = 0;
    i32 r = musttail_mixed(a, b, seed);
    struct musttail_pair pair = { 0u64, 0u64 };
    musttail_fill_twice(pair, (u64)seed);
    f64 d = 0.0;
    musttail_half_sum(d, seed, 2);
    musttail_any w = 0;
    musttail_aliased_out(w);
    return (x == 7) + (y == 15) + (z == 18) +
           (a == 11 && b == 40 && r == 3) +
           (pair.low == 8u64 && pair.high == 9u64) + (d == 3.0) + (w == 7);
}
