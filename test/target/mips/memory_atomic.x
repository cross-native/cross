// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 mips_global;
global u32 [[atomic]] mips_atomic;

[[noinline]]
global u32 mips_memory(in u32 *base, in u32 index, in u32 value) {
    base[index] = value;
    mips_global = base[index] + 1u32;
    return mips_global;
}

[[noinline]]
global u32 mips_atomic_add(in u32 value) {
    return $::atomic_fetch_add(
        &mips_atomic, value, $::memory::seq_cst);
}

global u32 mips_patch_value() {
    return $::patch(0x12345678u32) ^ 0x55aa55aau32;
}
