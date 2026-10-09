// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compiled under the 32-bit-bank entries of test/model/narrow32.y: every
// u64 and pointer crosses a boundary in two 32-bit pieces, and under
// narrow32_partial one value straddles the last register and the stack.

global volatile u64 narrow_input = 0x0123456789abcdefu64;
global u64 narrow_cells[4] = {1u64, 2u64, 3u64, 4u64};

[[noinline]]
global u64 narrow_mix(u64 a, u32 b, u64 c) {
    return a * 3u64 + (u64)b + (c >> 1u32);
}

[[noinline]]
global u64 narrow_straddle(u32 a, u32 b, u64 c) {
    return (u64)a * 1000u64 + (u64)b + (c ^ 0xff00ff00ff00ff00u64);
}

[[noinline]]
global u64 narrow_straddle_caller(u64 c) {
    return narrow_straddle(3u32, 4u32, c) + 1u64;
}

[[noinline]]
global u64 *narrow_pointer(u64 *p, u64 i) { return p + i; }

[[noinline]]
global u64 narrow_identity(u64 x) { return x; }

[[noinline]]
global u64 narrow_forward(u64 x, u64 y) {
    return narrow_identity(x) ^ narrow_mix(y, (u32)x, x + y);
}

[[noinline]]
global void narrow_out(out u64 r, u64 x) { r = x + 1u64; }

[[noinline]]
global u64 narrow_out_caller(u64 x) {
    u64 v;
    narrow_out(v, x);
    return v;
}

typedef u64 (*narrow_function)(u64 x);

global narrow_function narrow_indirect_target = narrow_identity;

[[noinline]]
global u64 narrow_indirect(u64 x) {
    narrow_function target = narrow_indirect_target;
    return target(x + 1u64) - 1u64;
}

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global i32 narrow_entry() {
    u64 x = narrow_input;
    u64 y = x * 5u64;
    i32 result = 0;
    if (narrow_mix(x, 7u32, y) == x * 3u64 + 7u64 + (y >> 1u32)) result |= 1;
    if (*narrow_pointer(narrow_cells, 2u64) == 3u64) result |= 2;
    if (narrow_identity(x) == x) result |= 4;
    if (narrow_forward(x, y) == (x ^ (y * 3u64 + (x & 0xffffffffu64) +
                                       ((x + y) >> 1u32)))) {
        result |= 8;
    }
    if (narrow_straddle_caller(x) ==
        3004u64 + (x ^ 0xff00ff00ff00ff00u64) + 1u64) {
        result |= 16;
    }
    if (narrow_indirect(x) == x) result |= 32;
    if (narrow_out_caller(x) == x + 1u64) result |= 64;
    return result;
}
