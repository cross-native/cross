// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] global i32 leaf(i32 x) {
    return x + 1;
}

[[noinline]] global i32 sink(u8 *bytes) {
    return bytes[0];
}

global i32 framed(i32 n) {
    u8 buffer[100];
    buffer[n] = 1u8;
    return sink(buffer) + leaf(n);
}

global i32 dynamic(u32 n) {
    stack u8 buffer[n];
    buffer[0] = 2u8;
    return sink(buffer);
}

#if defined(X86_64)
// A naked function counts what its own instructions push.
[[naked, clobber("memory", "flags")]]
global u64 pushes() -> "rax" {
    register u64 result "rax";
    $::_movabs(result, 7u64);
    $::_push(result);
    $::_pop(result);
    $::_ret();
}
#else
// A MIPS naked function has no stack.
[[naked, clobber("t0")]]
global u32 bare(in u32 base "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = base + 1u32;
    $::_jr(link);
}
#endif
