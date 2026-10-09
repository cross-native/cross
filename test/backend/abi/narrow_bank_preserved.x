// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global volatile u64 narrow_preserve_input = 0x0123456789abcdefu64;

// Under narrow32 a callee need keep only the low 32 bits of the registers
// call_clobbers omits; a 32-bit move clears the upper half.
[[abi("narrow32"), naked]]
global u32 narrow_scramble(in u32 x "edi") -> "eax" {
    register u32 result "eax";
    register u32 b "ebx";
    register u32 c "r12d";
    register u32 d "r13d";
    register u32 e "r14d";
    register u32 f "r15d";
    $::_mov(b, b);
    $::_mov(c, c);
    $::_mov(d, d);
    $::_mov(e, e);
    $::_mov(f, f);
    $::_mov(result, x);
    $::_ret();
}

// The values stay live across the narrow call.
[[abi("narrow32"), noinline]]
global u64 narrow_keep(u64 x) {
    u64 a = x * 3u64;
    u64 b = x ^ 0xfedcba9876543210u64;
    u32 r = narrow_scramble((u32)x);
    return a + b + (u64)r;
}

// A caller whose own ABI preserves whole registers saves them itself.
[[abi("cross"), noinline]]
global u64 wide_caller(u64 x) {
    return (u64)narrow_scramble((u32)x) + x;
}

[[abi("cross"), noinline]]
global u64 wide_keep(u64 x) {
    u64 a = x * 5u64;
    u64 b = x ^ 0x0f0f0f0f0f0f0f0fu64;
    u64 c = x + 0x1111111111111111u64;
    u64 r = wide_caller(x);
    return a + b + c + r;
}

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global i32 narrow_preserve_entry() {
    u64 x = narrow_preserve_input;
    i32 result = 0;
    if (narrow_keep(x) ==
        x * 3u64 + (x ^ 0xfedcba9876543210u64) + (x & 0xffffffffu64)) {
        result |= 1;
    }
    if (wide_keep(x) == x * 5u64 + (x ^ 0x0f0f0f0f0f0f0f0fu64) +
                            (x + 0x1111111111111111u64) +
                            (x & 0xffffffffu64) + x) {
        result |= 2;
    }
    return result;
}
