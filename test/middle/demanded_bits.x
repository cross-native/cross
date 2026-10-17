// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Masks, sets, and offsets whose effect no user reads are skipped, also
// through the phis of a loop. test_entry returns zero when every result
// matches the same operations performed one per call; it runs with
// -fno-eval-calls, so that the calls run.

[[noinline]] global u32 plus(in u32 a, in u32 b) { return a + b; }
[[noinline]] global u32 minus(in u32 a, in u32 b) { return a - b; }
[[noinline]] global u32 mask(in u32 a, in u32 b) { return a & b; }
[[noinline]] global u32 set(in u32 a, in u32 b) { return a | b; }
[[noinline]] global u32 flip(in u32 a, in u32 b) { return a ^ b; }
[[noinline]] global u32 right(in u32 a, in u32 b) { return a >> b; }
[[noinline]] global u32 left(in u32 a, in u32 b) { return a << b; }
[[noinline]] global i32 right_signed(in i32 a, in u32 b) { return a >> b; }

// A 16-bit heading and turn rate that the loop keeps masked; its users read
// only their low bits.
[[noinline]] global u32 headings(in u32 heading, in u32 turn, in uptr steps) {
    u32 total = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        heading = (heading + turn) & 65535u32;
        turn = (turn + 97u32) & 65535u32;
        const u32 ahead = (heading + 16384u32) & 65535u32;
        const u32 quadrant = (ahead >> 14) & 3u32;
        const u32 position = ahead & 16383u32;
        total = total * 31u32 + (quadrant ^ (position << 2));
    }
    return total ^ heading;
}

[[noinline]] global u32 headings_reference(in u32 heading, in u32 turn,
                                           in uptr steps) {
    u32 total = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        heading = mask(plus(heading, turn), 65535u32);
        turn = mask(plus(turn, 97u32), 65535u32);
        const u32 ahead = mask(plus(heading, 16384u32), 65535u32);
        const u32 quadrant = mask(right(ahead, 14u32), 3u32);
        const u32 position = mask(ahead, 16383u32);
        total = plus(total * 31u32, flip(quadrant, left(position, 2u32)));
    }
    return flip(total, heading);
}

[[noinline]] global u32 skipped_xor(in u32 x) { return ((x + 17u32) ^ 255u32) >> 13; }
[[noinline]] global u32 skipped_or(in u32 x) { return (x | 240u32) & 15u32; }
[[noinline]] global u32 skipped_add(in u32 x) { return (x + 256u32) & 255u32; }
[[noinline]] global u32 skipped_sub(in u32 x) { return ((x - 4096u32) << 20) >> 24; }
[[noinline]] global u32 kept_add(in u32 x) { return (x + 1u32) & 255u32; }
[[noinline]] global u32 kept_or(in u32 x) { return (x | 128u32) & 255u32; }
[[noinline]] global u32 kept_sign(in u32 x) {
    return (u32)((i32)((x & 65535u32) << 16) >> 24);
}
[[noinline]] global u32 truncated(in u32 x) { return (u32)(u8)((x + 768u32) | 65536u32); }
[[noinline]] global u64 widened(in u32 x) { return (u64)((x & 4095u32) + 1u32) << 52; }

// Tests of shifted bits read the bits in place.
[[noinline]] global bool bit_fourteen(in u32 x) { return ((x >> 14) & 1u32) != 0u32; }
[[noinline]] global bool two_bits(in u32 x) { return ((x >> 16) & 3u32) == 0u32; }
[[noinline]] global bool signed_bits(in i32 x) { return ((x >> 4) & 12) != 0; }
[[noinline]] global bool shifted_out(in u32 x) { return ((x >> 28) & 255u32) != 0u32; }
[[noinline]] global bool sign_copies(in i32 x) { return ((x >> 28) & 255) != 0; }

static const u32 probes[] = {
    0u32, 1u32, 2u32, 15u32, 16u32, 127u32, 128u32, 255u32, 256u32, 4095u32,
    4096u32, 16383u32, 16384u32, 32767u32, 32768u32, 65535u32, 65536u32,
    2147483647u32, 2147483648u32, 4294967295u32, 2654435769u32, 123456789u32,
};

global u32 test_entry() {
    if (headings(12345u32, 4321u32, 1000) !=
        headings_reference(12345u32, 4321u32, 1000)) {
        return 1u32;
    }
    if (headings(65535u32, 65535u32, 77) != headings_reference(65535u32, 65535u32, 77)) {
        return 2u32;
    }
    for (uptr k = 0; k < sizeof(probes) / sizeof(probes[0]); ++k) {
        const u32 x = probes[k];
        if (skipped_xor(x) != right(flip(plus(x, 17u32), 255u32), 13u32)) return 10u32;
        if (skipped_or(x) != mask(set(x, 240u32), 15u32)) return 11u32;
        if (skipped_add(x) != mask(plus(x, 256u32), 255u32)) return 12u32;
        if (skipped_sub(x) != right(left(minus(x, 4096u32), 20u32), 24u32)) return 13u32;
        if (kept_add(x) != mask(plus(x, 1u32), 255u32)) return 14u32;
        if (kept_or(x) != mask(set(x, 128u32), 255u32)) return 15u32;
        if (kept_sign(x) != (u32)right_signed((i32)left(mask(x, 65535u32), 16u32), 24u32)) {
            return 16u32;
        }
        if (truncated(x) != mask(set(plus(x, 768u32), 65536u32), 255u32)) return 17u32;
        if (widened(x) != (u64)plus(mask(x, 4095u32), 1u32) << 52) return 18u32;
        if (bit_fourteen(x) != (mask(right(x, 14u32), 1u32) != 0u32)) return 19u32;
        if (two_bits(x) != (mask(right(x, 16u32), 3u32) == 0u32)) return 20u32;
        if (signed_bits((i32)x) != ((right_signed((i32)x, 4u32) & 12) != 0)) return 21u32;
        if (shifted_out(x) != (mask(right(x, 28u32), 255u32) != 0u32)) return 22u32;
        if (sign_copies((i32)x) != ((right_signed((i32)x, 28u32) & 255) != 0)) return 23u32;
    }
    return 0u32;
}
