// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Functions that never return: they leave through the startup's exit with
// the status of the checks, so no return path follows their calls.

volatile u32 zero = 0u32;

[[abi("aapcs64"), noreturn]] global void semihost_exit(in u64 status);

[[noreturn, noinline]] static void finish(in u32 status, in u32 bias) {
    u32 table[64];
    for (u32 i = 0u32; i < 64u32; i = i + 1u32) table[i] = i * bias;
    semihost_exit(status + table[63] - 63u32 * bias);
}

[[noinline]] static u32 check(in u32 value) {
    if (value != 42u32) finish(1u32, value);
    return value + 1u32;
}

global u32 test_main() {
    const u32 z = zero;
    if (check(42u32 + z) != 43u32) finish(2u32, z);
    finish(z, 3u32);
}
