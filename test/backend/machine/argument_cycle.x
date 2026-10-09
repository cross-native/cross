// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Each caller forwards its own parameters in another order. When the
// parameters stay in their incoming registers, the narrow arguments form a
// register cycle that the call boundary breaks through a SIMD temporary.

[[noinline]]
global u32 argument_cycle_wide(in u32 a, in u32 b, in u8 c, in u8 d) {
    return a * 1000000u32 + b * 10000u32 + (u32)c * 100u32 + (u32)d;
}

[[noinline]]
global u32 argument_cycle_narrow(in u8 a, in u16 b, in u8 c, in u32 d) {
    return (u32)a * 1000000u32 + (u32)b * 1000u32 + (u32)c * 10u32 + d;
}

[[noinline]]
global u32 argument_cycle_swap_bytes(in u32 a, in u32 b, in u8 c, in u8 d) {
    return argument_cycle_wide(a, b, d, c);
}

[[noinline]]
global u32 argument_cycle_rotate(in u8 a, in u16 b, in u8 c, in u32 d) {
    return argument_cycle_narrow(c, (u16)a, (u8)b, d) + 1u32;
}

[[noinline]]
global u32 argument_cycle_swap(in u8 a, in u16 b, in u8 c, in u32 d) {
    return argument_cycle_narrow(c, b, a, d);
}

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global i32 argument_cycle_entry() {
    if (argument_cycle_swap_bytes(1u32, 2u32, 3u8, 4u8) != 1020403u32) {
        return 1;
    }
    if (argument_cycle_rotate(1u8, 2u16, 3u8, 4u32) != 3001025u32) return 2;
    if (argument_cycle_swap(5u8, 6u16, 7u8, 8u32) != 7006058u32) return 3;
    return 4;
}
