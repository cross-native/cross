// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The n64 slice: 64-bit addresses, 64-bit argument carriers, and the shared
// integer/floating argument cursor.  Every entry point here is called from
// n64_runtime.s under qemu-system-mips64.

global u64 n64_object = 0x0123456789abcdefu64;
global u64 *n64_object_address = &n64_object;
global u64 n64_sink;
global uptr n64_patch_address;

[[noinline]]
global u64 n64_patch_value() {
    return $::patch(0x123456789abcdef0u64, n64_patch_address);
}

// u32/i32/u64 arithmetic in one function: the 32-bit results must stay
// sign-canonical in their 64-bit registers, and the u64 add is native.
[[noinline]]
global u64 n64_arith(in u32 left, in i32 right, in u64 wide) {
    u32 narrow = left * 3u32 + 7u32;
    i32 signed_part = right - 5;
    u64 widened = narrow;
    u64 extended = signed_part;
    return wide + widened + extended;
}

// A pointer parameter and indexed memory: the address arithmetic must be
// daddu/dsll and the loads ld.
[[noinline]]
global u64 n64_sum_indexed(in const u64 *data, in uptr count) {
    u64 sum = 0u64;
    for (uptr index = 0; index < count; index = index + 1) {
        sum = sum + data[index];
    }
    return sum;
}

// A global object reached through %hi/%lo and a global pointer loaded with
// ld before it is dereferenced.
[[noinline]]
global u64 n64_globals(in u64 value) {
    u64 *cell = n64_object_address;
    u64 loaded = *cell;
    n64_sink = loaded ^ value;
    return n64_sink + n64_object;
}

// f64 and f32 arguments after an integer argument.  n64 shares one cursor,
// so `scale` lands in $f13 and `bias` in $f14 rather than in $5/$6.
[[noinline]]
global f64 n64_mixed_floats(in u32 base, in f64 scale, in f32 bias) {
    f64 widened = bias;
    f64 counted = base;
    return scale * 4.0f64 + widened + counted;
}

// Ten arguments: the first eight ride $4-$11, the ninth and tenth arrive in
// the caller's packed 8-byte slots at 0($sp) and 8($sp) with no home area.
[[noinline]]
global u64 n64_many_arguments(in u64 a0, in u64 a1, in u64 a2, in u64 a3,
                              in u64 a4, in u64 a5, in u64 a6, in u64 a7,
                              in u64 a8, in u64 a9) {
    return a0 + a1 * 2u64 + a2 * 3u64 + a3 * 4u64 + a4 * 5u64 +
           a5 * 6u64 + a6 * 7u64 + a7 * 8u64 + a8 * 9u64 + a9 * 10u64;
}

[[noinline]]
static u64 n64_leaf(in u64 value) {
    return value * 3u64 + 1u64;
}

// A call chain: the caller must keep a live value across two calls, which
// forces an sd/ld of $ra and of a callee-saved register.
[[noinline]]
global u64 n64_call_chain(in u64 seed) {
    u64 kept = seed + 100u64;
    u64 first = n64_leaf(seed);
    u64 second = n64_leaf(first);
    return kept + first + second;
}

// A pointer that lives across a call and an out parameter, so pointer PHI
// copies and pointer spills both appear.
[[noinline]]
global u64 n64_pointer_across_call(in u64 *cell, in u64 seed) {
    u64 folded = n64_leaf(seed);
    *cell = folded;
    return folded + cell[1];
}

[[noinline]]
global void n64_out_parameter(in u64 seed, out u64 result) {
    result = seed * 5u64 + 3u64;
}

// A surviving private definition: -fprivate-abi would otherwise re-classify
// it under a Cross convention whose address model is 32-bit, truncating the
// pointer channel.  Its caller is global, so only this edge is private.
[[noinline]]
static u64 n64_private_pointer(in u64 *cell, in u64 addend) {
    u64 loaded = cell[0] + cell[1];
    cell[2] = loaded + addend;
    return loaded + addend;
}

[[noinline]]
global u64 n64_private_entry(in u64 *cell, in u64 addend) {
    return n64_private_pointer(cell, addend) + cell[2];
}
