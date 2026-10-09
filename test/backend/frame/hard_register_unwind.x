// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Each function binds registers its ABI preserves to hard-register objects,
// one frame shape per function, and calls a probe that unwinds through it.

[[abi("ms_abi")]]
global void hard_unwind_probe();

global u64 hard_unwind_fixed() {
    register u64 first "r12" = 11;
    register u64 second "rbx" = 12;
    hard_unwind_probe();
    return first + second;
}

[[abi("ms_abi")]]
global u64 hard_unwind_win64() {
    register u64 first "rsi" = 21;
    register u64 second "rdi" = 22;
    register volatile f64 lane "xmm6" = 1.5;
    register volatile f64 other "xmm15" = 2.5;
    hard_unwind_probe();
    return first + second + (u64)(lane * other);
}

global u64 hard_unwind_dynamic(uptr count) {
    register u64 kept "r13" = 31;
    stack u8 values[count];
    values[0] = 3u8;
    hard_unwind_probe();
    return kept + values[0];
}

struct hard_unwind_cell [[aligned(64)]] {
    u64 value;
};

global u64 hard_unwind_realigned() {
    struct hard_unwind_cell cell;
    register u64 kept "rbx" = 41;
    cell.value = 1;
    hard_unwind_probe();
    return kept + cell.value;
}

// The CFA anchor of this frame must avoid r15.
global u64 hard_unwind_realigned_dynamic(uptr count) {
    struct hard_unwind_cell cell;
    register u64 kept "r15" = 51;
    stack u8 values[count];
    values[0] = 2u8;
    cell.value = 1;
    hard_unwind_probe();
    return kept + cell.value + values[0];
}

// COFF probes this frame's allocation.
global u64 hard_unwind_large() {
    stack u8 bytes[8192];
    register u64 kept "r14" = 61;
    bytes[0] = 1u8;
    bytes[8191] = 2u8;
    hard_unwind_probe();
    return kept + bytes[0] + bytes[8191];
}

// A leaf: ELF keeps its frame in the red zone.
global u64 hard_unwind_leaf(u64 seed) {
    register u64 kept "r14" = seed;
    kept *= 9;
    return kept;
}
