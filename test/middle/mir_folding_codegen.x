// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Code shapes of floating constant folding, constant-object reads, memory
// value forwarding, conditional replacement, and shared store addresses.

global f32 reciprocal() { return 1.0f32 / 3.0f32; }

static const f32 scale = 1.0f32 / 65536.0f32;
global f32 scaled(in f32 x) { return x * scale; }

global u32 counter;
global u32 state[4];
global u32 bump() {
    counter = counter + 1u32;
    state[1] = state[0] ^ counter;
    return state[1] + state[2];
}

global i32 below(in i32 x) { return x < 10 ? 1 : 0; }

global u32 seeds[55];
global void refresh() {
    for (u32 i = 0u32; i < 24u32; ++i) {
        seeds[i] = seeds[i] - seeds[i + 31u32];
    }
}

global f32 eighth(in f32 x) { return x / 8.0f32; }

// Each raises an exception that one of the test profiles traps.
global f32 overflow() { return 1.0e38f32 * 10.0f32; }
global f32 infinite() { return 1.0f32 / 0.0f32; }
global f32 denormal_product() { return 1.0e-40f32 * 2.0f32; }
global f32 tiny_product() { return 1.0e-30f32 * 1.0e-10f32; }

struct pair { u32 first; u32 second; };
global u32 second(in struct pair *p) { return p->second; }
global const struct pair constant_pair = {5u32, 9u32};

// Reads of these const objects stay: a weak definition may be replaced at
// link time, and -fzero-init-in-data keeps an all-zero object patchable.
[[weak]] global const u32 replaceable = 6u32;
global u32 read_replaceable() { return replaceable; }
global const u32 zero_word = 0u32;
global u32 read_zero_word() { return zero_word; }
global u32 constant_tag() { return constant_pair.second; }

// The shifted seed has a clear top bit and converts as a signed word.
global f32 scaled_seed(in u32 seed) { return (f32)(seed >> 20) * 0.00390625f32; }

// Each pass reads the value the previous pass stored.
global u32 cursor;
global u32 carried_cursor(in u32 n) {
    cursor = 3u32;
    u32 total = 0u32;
    for (u32 i = 0u32; i < n; ++i) {
        const u32 at = cursor == 0u32 ? 54u32 : cursor - 1u32;
        total = total * 7u32 + at;
        cursor = at;
    }
    return total;
}

// The stored bits read back as another type of their width.
union word_view { f32 value; u32 bits; };
global u32 word_of(in f32 x) {
    union word_view cell;
    cell.value = x;
    return cell.bits;
}
