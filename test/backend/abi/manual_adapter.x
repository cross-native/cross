// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Named functions converted to function-pointer types with another callable
// interface: each conversion calls the function through a private adapter.

typedef u64 (*plain_fn)(in u64 a, in u64 b);
typedef u64 (*manual_fn)(in u64 a "r12", in u64 b "stack") -> "*r9";
typedef u64 (*own_fn)(in u64 a "rdi", in u64 b "rsi") -> "rax";
typedef u64 (*cleanup_fn)(in u64 a "push=>discard", in u64 b "r10") -> "rax"
    [[stack_cleanup("callee"), clobber("r11")]];
typedef void (*mode_fn)(in u64 a "rdx", inout u64 b "rcx", out u64 c "*r8");
typedef void (*plain_mode_fn)(in u64 a, inout u64 b, out u64 c);

u64 manual_source(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 100u64 + b;
}

u64 manual_other(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 200u64 + b;
}

[[noinline]] global u64 global_manual(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 300u64 + b;
}

[[abi("sysv_abi")]] global u64 sysv_source(in u64 a, in u64 b) {
    return a * 1000u64 + b;
}

u64 plain_source(in u64 a, in u64 b) {
    return a + b * 2u64;
}

void plain_modes(in u64 a, inout u64 b, out u64 c) {
    c = a + b;
    b = b * 3u64;
}

void manual_modes(in u64 a "rdx", inout u64 b "rcx", out u64 c "*r8") {
    c = a * b;
    b = b + 1u64;
}

[[generic(plain_fn F), noinline]]
static u64 invoke_plain(in u64 a, in u64 b) { return F(a, b); }

[[generic(manual_fn F), noinline]]
static u64 invoke_manual(in u64 a, in u64 b) { return F(a, b); }

[[noinline]] u64 call_plain(in plain_fn f, in u64 a, in u64 b) {
    u64 keep = a ^ b;
    return f(a, b) + keep;
}

[[noinline]] u64 call_manual(in manual_fn f, in u64 a, in u64 b) {
    u64 keep = a ^ b;
    return f(a, b) + keep;
}

[[noinline]] u64 call_own(in own_fn f, in u64 a, in u64 b) {
    return f(a, b);
}

[[noinline]] u64 call_cleanup(in cleanup_fn f, in u64 a, in u64 b) {
    u64 total = 0u64;
    for (u64 i = 0u64; i < 3u64; i += 1u64) total += f(a + i, b);
    return total;
}

[[noinline]] u64 call_modes(in mode_fn f, in u64 a) {
    u64 b = a + 1u64;
    u64 c = 0u64;
    f(a, b, c);
    return b * 1000u64 + c;
}

[[noinline]] u64 call_plain_modes(in plain_mode_fn f, in u64 a) {
    u64 b = a + 1u64;
    u64 c = 0u64;
    f(a, b, c);
    return b * 1000u64 + c;
}

struct table {
    u32 tag;
    plain_fn first;
    manual_fn second;
    plain_fn rest[2];
};

struct pair {
    plain_fn first;
    plain_fn second;
};

struct chain {
    struct pair inner;
    plain_fn next;
};

plain_fn plain_table[3] = { manual_source, [2] = &global_manual, [1] = plain_source };
manual_fn manual_table[2] = { plain_source, &sysv_source };
struct table record_table = { 7u32, manual_source, .second = manual_source,
                              { plain_source, manual_other } };
// After a designator chain, the next entry initializes the member that
// follows the chain's first member.
struct chain chain_table = { .inner.first = manual_source, manual_other };
cleanup_fn saved_cleanup = plain_source;
u64 zero = 0u64;

global u32 manual_adapter_entry() {
    const u64 one = zero + 1u64;
    plain_fn p = manual_source;
    if (call_plain(p, one + 2u64, 4u64) != 304u64 + 7u64) return 1u32;
    if (call_plain(&manual_source, one + 4u64, 6u64) != 506u64 + 3u64) return 2u32;
    if (p != (plain_fn)&manual_source || p != manual_source) return 3u32;
    // Conditional arms decay to their own interface, which they must share.
    own_fn chosen = one != 0u64 ? manual_source : manual_other;
    if (call_own(chosen, one, 2u64) != 102u64) return 4u32;
    own_fn either = zero != 0u64 ? manual_source : global_manual;
    if (either != global_manual || call_own(either, one, 2u64) != 302u64) return 5u32;
    manual_fn m = plain_source;
    if (call_manual(m, one + 2u64, 4u64) != 11u64 + 7u64) return 6u32;
    if (call_manual(sysv_source, one + 2u64, 4u64) != 3004u64 + 7u64) return 7u32;
    if (call_manual(manual_source, one + 6u64, 8u64) != 708u64 + 15u64) return 8u32;
    if (call_own(manual_source, one + 1u64, 9u64) != 209u64) return 9u32;
    if (call_own(global_manual, one + 1u64, 9u64) != 609u64) return 10u32;
    if (call_plain(plain_table[0], one, one) != 101u64 ||
        call_plain(plain_table[1], one, one) != 3u64 ||
        call_plain(plain_table[2], one, one) != 301u64) return 11u32;
    if (call_manual(manual_table[0], one + 1u64, 3u64) != 8u64 + 1u64 ||
        call_manual(manual_table[1], one + 1u64, 3u64) != 2003u64 + 1u64) return 12u32;
    if (call_plain(record_table.first, one, one) != 101u64 ||
        call_manual(record_table.second, one, one) != 101u64 ||
        call_plain(record_table.rest[0], one, one) != 3u64 ||
        call_plain(record_table.rest[1], one, one) != 201u64) return 13u32;
    struct chain local_chain = { .inner.second = manual_other, manual_source };
    if (call_plain(chain_table.inner.first, one, one) != 101u64 ||
        call_plain(chain_table.next, one, one) != 201u64 ||
        call_plain(local_chain.inner.second, one, one) != 201u64 ||
        call_plain(local_chain.next, one, one) != 101u64) return 19u32;
    if (call_cleanup(saved_cleanup, one, 5u64) != 11u64 + 12u64 + 13u64) return 14u32;
    if (call_modes(plain_modes, one + 3u64) != 15000u64 + 9u64) return 15u32;
    if (call_plain_modes(manual_modes, one + 3u64) != 6000u64 + 20u64) return 16u32;
    if (call_modes(manual_modes, one + 3u64) != 6000u64 + 20u64) return 17u32;
    if (invoke_plain::<manual_source>(one, 2u64) != 102u64 ||
        invoke_manual::<plain_source>(one, 2u64) != 5u64 ||
        invoke_manual::<manual_other>(one, 2u64) != 202u64) return 18u32;
    return 200u32;
}
