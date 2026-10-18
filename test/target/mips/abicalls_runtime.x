// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Abicalls and thread-local code at run time: objects and functions of
// every binding through the GOT, calls through $t9 to functions of this
// unit, a partner unit, and the startup, function pointers both ways, label
// addresses, $gp after a callee that changes it, and thread-local objects
// of both TLS models in .tdata and .tbss, also the partner's. Without
// -mabicalls the same accesses are absolute and local-exec. Each round
// returns 1 when every value agrees.

global u32 exported = 100u32;
global u32 exported_table[4] = {1u32, 2u32, 3u32, 4u32};
static u32 hidden = 5u32;
static u32 hidden_table[64];
u32 shared = 11u32;
global u64 wide = 0x123456789abcdef0u64;

[[thread_local]] global u32 tls_counter;
[[thread_local]] static u32 tls_seed = 40u32;
[[thread_local]] global u64 tls_wide = 0x1122334455667788u64;
[[thread_local, tls_model("local-exec")]] global u32 tls_table[4] = {1u32, 2u32, 3u32, 4u32};
[[thread_local, tls_model("initial-exec")]] static u8 tls_bytes[3];

// The partner unit's definitions.
u32 partner_value;
u32 partner_table[];
[[thread_local]] u32 partner_tls;
u32 partner_add(u32 a, u32 b);
u32 partner_apply(u32 (*f)(u32 x), u32 x);
u32 partner_twice(u32 x);
u32 partner_tls_bump(u32 step);
// The startup's: a function that changes $gp and returns its argument.
u32 scramble_gp(u32 x);

[[noinline]] static u32 square(u32 x) { return x * x; }
[[noinline]] u32 triple(u32 x) { return x * 3u32 + shared; }
global u32 twice(u32 x) { return x + x; }
[[noinline]] global u32 forward(u32 x) { return partner_add(x, hidden); }

[[noinline]] static u32 pick(u32 which) {
    label targets[3] = {pick::first, pick::second, pick::third};
    goto targets[which % 3u32];
first:
    return 10u32;
second:
    return 20u32;
third:
    return 30u32;
}

[[noinline]] static u32 jump(u32 which) {
    label target = which == 0u32 ? jump::zero : jump::other;
    goto target;
zero:
    return 7u32;
other:
    return 9u32;
}

// A result in $v0 and $v1 next to the thread pointer, which `rdhwr` reads
// into $v1.
[[noinline]] static u64 wide_tls(u32 round) { return tls_wide + (u64)round; }
[[noinline]] static u32* tls_address(u32 round) { return &tls_table[round & 3u32]; }

global u32 runtime_check(u32 round) {
    u32 ok = 1u32;
    hidden_table[round] = round + 1u32;
    exported = exported + 1u32;
    hidden = hidden + exported_table[round & 3u32];
    if (exported != 101u32 + round) ok = 0u32;
    if (hidden_table[round] != round + 1u32) ok = 0u32;
    // A callee that changes $gp, then objects through the GOT again.
    if (scramble_gp(round) != round) ok = 0u32;
    if (shared != 11u32 || exported_table[3] != 4u32) ok = 0u32;
    if (partner_value != 7u32 || partner_table[2] != 30u32) ok = 0u32;
    if (square(round + 2u32) != (round + 2u32) * (round + 2u32)) ok = 0u32;
    if (triple(round) != 3u32 * round + 11u32) ok = 0u32;
    if (forward(round) != round + hidden + 7u32) ok = 0u32;
    if (partner_twice(round) != 2u32 * round) ok = 0u32;
    u32 (*f)(u32 x) = round == 1u32 ? twice : triple;
    if (partner_apply(f, 5u32) != (round == 1u32 ? 11u32 : 27u32)) ok = 0u32;
    if (partner_apply(square, round) != round * round + 1u32) ok = 0u32;
    u32* where = round == 2u32 ? &exported : &hidden_table[round];
    *where = *where + 2u32;
    if (round == 2u32 && exported != 105u32) ok = 0u32;
    if (round != 2u32 && hidden_table[round] != round + 3u32) ok = 0u32;
    if (pick(round) != 10u32 * (round + 1u32)) ok = 0u32;
    if (jump(round) != (round == 0u32 ? 7u32 : 9u32)) ok = 0u32;
    if (wide + (u64)round != 0x123456789abcdef0u64 + (u64)round) ok = 0u32;

    // Thread-local objects keep their values across rounds and calls.
    tls_counter = tls_counter + 1u32;
    tls_seed = tls_seed + round;
    const u32 bumped = partner_tls_bump(2u32);
    if (tls_counter != 3u32 * (round + 1u32)) ok = 0u32;
    if (bumped != 11u32 + 2u32 * round || partner_tls != bumped) ok = 0u32;
    if (tls_seed != 40u32 + round * (round + 1u32) / 2u32) ok = 0u32;
    tls_wide = tls_wide + 1u64;
    if (wide_tls(round) != 0x1122334455667789u64 + 2u64 * (u64)round) ok = 0u32;
    tls_table[round] = tls_table[round] * 10u32;
    if (tls_table[round] != 10u32 * (round + 1u32) || tls_table[3] != 4u32) ok = 0u32;
    if (tls_address(round) != &tls_table[round] || *tls_address(round) != tls_table[round]) {
        ok = 0u32;
    }
    tls_bytes[round] = (u8)(round + 7u32);
    if (tls_bytes[0] != 7u8 || tls_bytes[round] != (u8)(round + 7u32)) ok = 0u32;
    return ok;
}
