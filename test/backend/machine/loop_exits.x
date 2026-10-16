// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops whose exits carry work and phi copies: a rotated test falls through
// to its exit, scheduling keeps the exit's work out of the loop, and a MIPS
// branch fills its delay slot when only its other edge needs copies.
// test_entry returns zero when every result matches; it runs with
// -fno-eval-calls, so that the calls run.

[[noinline]] global u32 opaque(in u32 value) { return value; }

[[noinline]] global u32 mix_steps(in u32 seed, in u32 count) {
    u32 value = seed;
    u32 step = 1u32;
    for (u32 index = 0u32; index < count; ++index) {
        value = (value ^ (value >> 7u32)) * 2654435761u32 + step;
        step = step + 3u32;
    }
    return value ^ step;
}

[[noinline]] global u32 nested_exit_work(in u32 extent) {
    u32 total = 0u32;
    for (u32 outer = 0u32; outer < extent; ++outer) {
        for (u32 inner = 0u32; inner < outer + 3u32; ++inner) {
            total += (outer ^ inner) + 1u32;
        }
        total ^= outer * 5u32;
    }
    return total;
}

[[noinline]] global u32 divide_steps(in u32 state, in u32 iterations) {
    u32 result = state;
    u32 sequence = state;
    u32 divisor = 3u32;
    u32 step = 1u32;
    for (u32 index = 0u32; index < iterations; ++index) {
        const u32 quotient = result / divisor;
        const u32 remainder = result % divisor;
        result = (quotient ^ (remainder * 2654435761u32)) + sequence;
        sequence = sequence * 1664525u32 + step;
        divisor = (sequence & 1023u32) + 1u32;
        step = step + 1u32;
    }
    return result ^ sequence;
}

global u32 test_entry() {
    if (mix_steps(opaque(12345u32), opaque(0u32)) != 0x00003038u32) return 1u32;
    if (mix_steps(opaque(12346u32), opaque(1u32)) != 0x0651f83fu32) return 2u32;
    if (mix_steps(opaque(12347u32), opaque(3u32)) != 0x19acc1e8u32) return 3u32;
    if (mix_steps(opaque(12348u32), opaque(4u32)) != 0x30050fd3u32) return 4u32;
    if (mix_steps(opaque(12349u32), opaque(5u32)) != 0xe197fc2bu32) return 5u32;
    if (mix_steps(opaque(12350u32), opaque(7u32)) != 0x1dbdf4afu32) return 6u32;
    if (mix_steps(opaque(12351u32), opaque(9u32)) != 0xa518f0fcu32) return 7u32;
    if (mix_steps(opaque(12352u32), opaque(13u32)) != 0x610a7461u32) return 8u32;
    if (nested_exit_work(opaque(0u32)) != 0u32) return 9u32;
    if (nested_exit_work(opaque(1u32)) != 6u32) return 10u32;
    if (nested_exit_work(opaque(2u32)) != 21u32) return 11u32;
    if (nested_exit_work(opaque(3u32)) != 44u32) return 12u32;
    if (nested_exit_work(opaque(4u32)) != 74u32) return 13u32;
    if (nested_exit_work(opaque(5u32)) != 126u32) return 14u32;
    if (nested_exit_work(opaque(6u32)) != 187u32) return 15u32;
    if (nested_exit_work(opaque(7u32)) != 240u32) return 16u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(0u32)) != 0x00000000u32) return 17u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(1u32)) != 0xd495462au32) return 18u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(2u32)) != 0x5bf6bdcau32) return 19u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(5u32)) != 0x6575fab0u32) return 20u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(6u32)) != 0xa5134812u32) return 21u32;
    if (divide_steps(opaque(0x9e3779b9u32), opaque(11u32)) != 0x06ee5dbeu32) return 22u32;
    return 0u32;
}
