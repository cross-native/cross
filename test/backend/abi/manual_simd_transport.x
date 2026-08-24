// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 pointer_target = 0u64;

global uptr xor_in_simd(in uptr value "xmm1") -> "xmm0" {
    return value ^ 0x55aa55aa55aa55aauptr;
}

global u64 *pointer_in_simd(in u64 *value "xmm1") -> "xmm0" {
    return value;
}

global i32 simd_transport_entry() {
    uptr transformed =
        xor_in_simd(0x123456789abcdef0uptr);
    register uptr hard_value "xmm3" =
        0x0f0e0d0c0b0a0908uptr;
    hard_value ^= 0x0101010101010101uptr;
    return transformed == 0x479e03d2cf168b5auptr &&
           hard_value == 0x0e0f0c0d0a0b0809uptr;
}
