// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads replaced by the value a dominating store or load left in memory, and
// reads of constant objects replaced by their initial values, stay correct
// around may-alias stores, calls, volatile accesses, branches, loops, and
// differently typed views. Stores share the address recurrence of the loads
// of their element. test_entry returns zero when every result matches. It
// runs with -fno-eval-calls, so that the calls run.

global u32 counter;
global u32 state[4];
global u32 other;
global volatile u32 device;
global u32 seeds[55];

struct pair { u32 first; u32 second; u16 low; u16 high; };

static const f32 scale = 1.0f32 / 65536.0f32;
global const f32 global_scale = 0.25f32;
global const u32 table[5] = {3u32, 1u32, 4u32, 1u32, 5u32};
global const struct pair constant_pair = {7u32, 9u32, 0x1234u16, 0xabcdu16};
global const u32 word = 0x11223344u32;
global const u16 halves[2] = {0x5566u16, 0x7788u16};
[[weak]] global const u32 replaceable = 6u32;

union view { f32 value; u32 bits; };

[[noinline]] global void touch() { counter = counter + 100u32; }
[[noinline]] global u8 byte_at(in const u8 *p, in u32 i) { return p[i]; }

[[noinline]] global u32 bump() {
    counter = counter + 1u32;
    state[1] = state[0] ^ counter;
    return state[1] + state[2];
}

// p may point to counter.
[[noinline]] global u32 aliased(in u32 *p) {
    counter = 1u32;
    *p = 2u32;
    return counter;
}

[[noinline]] global u32 across_call() {
    counter = 1u32;
    touch();
    return counter;
}

[[noinline]] global u32 across_volatile() {
    device = 1u32;
    const u32 first = device;
    device = 2u32;
    return first * 10u32 + device;
}

[[noinline]] global u32 offsets(in u32 *p, in u32 a, in u32 b) {
    p[0] = a;
    p[1] = b;
    return p[0] * 10u32 + p[1];
}

[[noinline]] global u32 members(in struct pair *p, in u32 v) {
    p->first = v;
    p->second = v + 1u32;
    p->low = 3u16;
    return p->first + p->second + (u32)p->low;
}

// A branch that writes another location keeps counter available; one that
// may write it through p does not.
[[noinline]] global u32 dominating(in bool c, in u32 *p) {
    counter = 5u32;
    if (c) other = 1u32;
    if (c) *p = 7u32;
    return counter;
}

[[noinline]] global u32 joined(in bool c) {
    if (c) counter = 1u32;
    else counter = 2u32;
    return counter;
}

[[noinline]] global u32 looped(in u32 n) {
    counter = 0u32;
    u32 sum = 0u32;
    for (u32 i = 0u32; i < n; ++i) {
        sum += counter;
        counter = sum + i;
    }
    return sum + counter;
}

[[noinline]] global u32 reinterpreted(in f32 x) {
    union view cell;
    cell.value = x;
    return cell.bits;
}

[[noinline]] global u32 local_arrays(in u32 n) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < n; ++i) {
        u32 local[2];
        local[0] = i;
        local[1] = local[0] + 1u32;
        total += local[1];
    }
    return total;
}

[[noinline]] global void refresh() {
    for (u32 i = 0u32; i < 24u32; ++i) {
        seeds[i] = seeds[i] - seeds[i + 31u32];
    }
}

[[noinline]] global f32 scaled(in f32 x) { return x * scale + global_scale; }

[[noinline]] global u32 constants(in u32 i) {
    const u32 *p = table;
    return table[2] * 1000u32 + p[4] * 100u32 + constant_pair.second * 10u32 +
           (u32)constant_pair.high + table[i];
}

global u32 test_entry() {
    counter = 0u32;
    state[0] = 5u32;
    state[2] = 7u32;
    if ($::runtime(bump()) != (5u32 ^ 1u32) + 7u32 || state[1] != 4u32 ||
        counter != 1u32) return 1u32;
    if ($::runtime(aliased(&counter)) != 2u32) return 2u32;
    if ($::runtime(aliased(&other)) != 1u32 || other != 2u32) return 3u32;
    if ($::runtime(across_call()) != 101u32) return 4u32;
    if ($::runtime(across_volatile()) != 12u32) return 5u32;
    u32 cells[2];
    if ($::runtime(offsets(cells, 3u32, 4u32)) != 34u32) return 6u32;
    if ($::runtime(offsets(&cells[0], 8u32, 9u32)) != 89u32 || cells[1] != 9u32)
        return 7u32;
    struct pair record;
    if ($::runtime(members(&record, 10u32)) != 24u32 || record.second != 11u32)
        return 8u32;
    if ($::runtime(dominating((bool)0u32, &counter)) != 5u32) return 9u32;
    if ($::runtime(dominating((bool)1u32, &other)) != 5u32) return 10u32;
    if ($::runtime(dominating((bool)1u32, &counter)) != 7u32) return 11u32;
    if ($::runtime(joined((bool)1u32)) != 1u32 || $::runtime(joined((bool)0u32)) != 2u32)
        return 12u32;
    if ($::runtime(looped(4u32)) != 11u32) return 13u32;
    if ($::runtime(reinterpreted(1.0f32)) != 0x3f800000u32) return 14u32;
    if ($::runtime(local_arrays(4u32)) != 10u32) return 15u32;
    for (u32 i = 0u32; i < 55u32; ++i) seeds[i] = i * i;
    $::runtime(refresh());
    for (u32 i = 0u32; i < 24u32; ++i) {
        if (seeds[i] != i * i - (i + 31u32) * (i + 31u32)) return 16u32;
    }
    if ($::runtime(scaled(65536.0f32)) != 1.25f32) return 17u32;
    if ($::runtime(constants(3u32)) != 4000u32 + 500u32 + 90u32 + 0xabcdu32 + 1u32)
        return 18u32;
    // A byte of a constant object reads in the target byte order.
    const u8 *bytes = (const u8 *)(const void *)&word;
    if (bytes[0] != $::runtime(byte_at(bytes, 0u32)) ||
        bytes[3] != $::runtime(byte_at(bytes, 3u32))) return 19u32;
    const u8 *half_bytes = (const u8 *)(const void *)halves;
    if (half_bytes[2] != $::runtime(byte_at(half_bytes, 2u32))) return 20u32;
    if (replaceable != 6u32) return 21u32;
    return 0u32;
}
