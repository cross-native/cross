// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls through function pointers whose types carry manual endpoints, extra
// clobbers, or callee stack cleanup. Each caller keeps values live across the
// call so that a register the call sequence writes cannot hold them.

typedef u64 (*register_fn)(in u64 a "rdi", in u64 b "rsi") -> "rax";
typedef u64 (*memory_fn)(in u64 a "rdi") -> "*r9";
typedef u64 (*stack_fn)(in u64 a "stack", inout u64 b "stack+8",
                        out u64 c "*r11") -> "stack+16";
typedef void (*cleanup_fn)(in u64 a "push=>discard", out u64 r "r10")
    [[stack_cleanup("callee")]];
typedef u64 (*clobber_fn)(in u64 a "rdi") -> "rax"
    [[clobber("rbx", "rsi", "r12", "r13", "r14", "r15", "flags")]];
typedef u64 (*plain_clobber_fn)(in u64 a)
    [[clobber("rbx", "rsi", "rdi", "r12", "r13", "r14", "r15", "flags")]];
typedef f80 (*x87_fn)(in f80 a "st0", in f80 b "st1") -> "st0";

[[noinline]] global u64 register_target(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 10u64 + b;
}

[[noinline]] global u64 memory_target(in u64 a "rdi") -> "*r9" {
    return a * 3u64 + 1u64;
}

[[noinline]] global u64 stack_target(in u64 a "stack", inout u64 b "stack+8",
                                     out u64 c "*r11") -> "stack+16" {
    c = a + b;
    b = b * 2u64;
    return a * 7u64;
}

[[noinline, stack_cleanup("callee")]]
global void cleanup_target(in u64 a "push=>discard", out u64 r "r10") {
    r = a + 5u64;
}

// Overwrites registers the platform ABIs preserve, as declared.
[[naked, clobber("rbx", "rsi", "r12", "r13", "r14", "r15", "flags")]]
global u64 clobber_target(in u64 a "rdi") -> "rax" {
    register u64 result "rax";
    register u64 b "rbx";
    register u64 s "rsi";
    register u64 c "r12";
    register u64 d "r13";
    register u64 e "r14";
    register u64 g "r15";
    $::_mov(result, a);
    $::_movabs(b, 0x1111u64);
    $::_movabs(s, 0x6666u64);
    $::_movabs(c, 0x2222u64);
    $::_movabs(d, 0x3333u64);
    $::_movabs(e, 0x4444u64);
    $::_movabs(g, 0x5555u64);
    $::_add(result, 100);
    $::_ret();
}

// A managed definition may clobber what it declares instead of saving it.
[[noinline, clobber("rbx", "rsi", "rdi", "r12", "r13", "r14", "r15", "flags")]]
global u64 plain_clobber_target(in u64 a) {
    return clobber_target(a) + 100u64;
}

[[noinline]] global f80 x87_target(in f80 a "st0", in f80 b "st1") -> "st0" {
    return a * 2.0f80 + b;
}

// A direct call writes its input endpoint, here a register every platform
// ABI preserves.
[[noinline]] global u64 preserved_input(in u64 a "rbx") -> "rax" {
    return a + 1u64;
}

register_fn saved_register = register_target;
memory_fn saved_memory = memory_target;
stack_fn saved_stack = stack_target;
cleanup_fn saved_cleanup = cleanup_target;
clobber_fn saved_clobber = clobber_target;
plain_clobber_fn saved_plain_clobber = plain_clobber_target;
x87_fn saved_x87 = x87_target;
// Runtime storage keeps the calls below from being evaluated at translation time.
u64 zero = 0u64;

[[noinline]] u64 use_register(in register_fn f, in u64 x) {
    u64 p = x * 3u64;
    u64 q = x ^ 5u64;
    u64 s = x + 11u64;
    u64 t = x * 13u64;
    u64 u = x ^ 17u64;
    u64 v = x + 19u64;
    u64 w = x * 23u64;
    u64 r = f(x, 4u64);
    return r + p + q + s + t + u + v + w;
}

[[noinline]] u64 use_memory(in memory_fn f, in u64 x) {
    u64 p = x + 9u64;
    u64 r = f(x);
    return r + p;
}

[[noinline]] u64 use_stack(in stack_fn f, in u64 x) {
    u64 b = x + 1u64;
    u64 c = 0u64;
    u64 r = f(x, b, c);
    return r * 1000000u64 + b * 1000u64 + c;
}

[[noinline]] u64 use_cleanup(in cleanup_fn f, in u64 x) {
    u64 total = 0u64;
    for (u64 i = 0u64; i < 4u64; i += 1u64) {
        u64 r = 0u64;
        f(x + i, r);
        total += r;
    }
    return total;
}

[[noinline]] u64 use_clobber(in clobber_fn f, in u64 x) {
    u64 a = x + 1u64;
    u64 b = x * 3u64;
    u64 c = x ^ 0x55u64;
    u64 d = x + 17u64;
    u64 e = x * 5u64;
    u64 g = x + 99u64;
    u64 r = f(x);
    return r + a + b + c + d + e + g;
}

[[noinline]] u64 use_plain_clobber(in plain_clobber_fn f, in u64 x) {
    u64 a = x + 1u64;
    u64 b = x * 3u64;
    u64 c = x ^ 0x55u64;
    u64 d = x + 17u64;
    u64 e = x * 5u64;
    u64 g = x + 99u64;
    u64 r = f(x);
    return r + a + b + c + d + e + g;
}

[[noinline]] f80 use_x87(in x87_fn f, in f80 x) {
    f80 keep = x + 1.0f80;
    return f(x, 3.0f80) + keep;
}

[[noinline]] u64 use_preserved_input(in u64 x, in u64 y) {
    u64 p = x * y;
    u64 q = x ^ y;
    u64 s = x + 11u64;
    u64 t = y * 13u64;
    u64 u = x ^ 17u64;
    u64 v = y + 19u64;
    u64 w = x * 23u64;
    u64 r = preserved_input(x);
    return r + p + q + s + t + u + v + w;
}

global u32 manual_indirect_entry() {
    if (use_register(saved_register, zero + 7u64) != 74u64 + 21u64 + (7u64 ^ 5u64) +
        18u64 + 91u64 + (7u64 ^ 17u64) + 26u64 + 161u64) return 1u32;
    if (use_memory(saved_memory, zero + 5u64) != 16u64 + 14u64) return 2u32;
    if (memory_target(zero + 6u64) != 19u64) return 3u32;
    if (use_stack(saved_stack, zero + 3u64) !=
        21u64 * 1000000u64 + 8u64 * 1000u64 + 7u64) return 4u32;
    if (use_cleanup(saved_cleanup, zero + 10u64) != 15u64 + 16u64 + 17u64 + 18u64)
        return 5u32;
    const u64 kept = 3u64 + 6u64 + (2u64 ^ 0x55u64) + 19u64 + 10u64 + 101u64;
    if (use_clobber(saved_clobber, zero + 2u64) != 102u64 + kept) return 6u32;
    if (use_plain_clobber(saved_plain_clobber, zero + 2u64) != 202u64 + kept)
        return 7u32;
    if (use_x87(saved_x87, (f80)(zero + 5u64)) != 19.0f80) return 8u32;
    if (use_preserved_input(zero + 6u64, zero + 7u64) != 7u64 + 42u64 + (6u64 ^ 7u64) +
        17u64 + 91u64 + (6u64 ^ 17u64) + 26u64 + 138u64) return 9u32;
    return 200u32;
}
