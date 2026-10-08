// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Promoted `out`/`inout` cells stay in registers.

global void copy_out_hash(in const u32 *values, in u32 count,
                          inout u32 hash) {
    for (u32 index = 0u32; index < count; index += 1u32) {
        hash = (hash ^ values[index]) * 16777619u32;
    }
}

global void copy_out_count(in const u32 *values, in u32 count,
                           out u32 matches) {
    matches = 0u32;
    for (u32 index = 0u32; index < count; index += 1u32) {
        if (values[index] > 9u32) matches += 1u32;
    }
}

// Straight-line copy-out stores through the transport pointer directly.
global void copy_out_quotient(in u32 a, in u32 b, out u32 quotient) {
    quotient = a / b;
}

global void copy_out_bump(inout u32 value) {
    value += 1u32;
}

global u32 copy_out_status(in u32 a, in u32 b, out u32 remainder) {
    remainder = a % b;
    return a / b;
}
