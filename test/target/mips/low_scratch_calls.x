// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Values live across a call whose ABI preserves t0 and t1 stay out of them,
// because the caller composes the call's result through them.
// low_scratch_calls.xm describes the callee's ABI.

global u32 data[9];

[[noinline, abi("keep_t01")]] global u32 produce(in u32 x) {
    return x * 3u32 + 70000u32;
}

global u32 test_entry() {
    const u32 a0 = data[0] + 1u32;
    const u32 a1 = data[1] + 2u32;
    const u32 a2 = data[2] + 3u32;
    const u32 a3 = data[3] + 4u32;
    const u32 a4 = data[4] + 5u32;
    const u32 a5 = data[5] + 6u32;
    const u32 a6 = data[6] + 7u32;
    const u32 a7 = data[7] + 8u32;
    const u32 a8 = data[8] + 9u32;
    const u32 r = produce(data[0] + 5u32);
    const u32 sum = a0 + a1 * 3u32 + a2 * 5u32 + a3 * 7u32 + a4 * 11u32 +
                    a5 * 13u32 + a6 * 17u32 + a7 * 19u32 + a8 * 23u32;
    if (sum != 661u32) return 1u32;
    if (r != 70015u32) return 2u32;
    return 0u32;
}
