// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Naked and raw_inline bodies read, write, and take the address of
// static-duration objects. test_entry returns zero when every result
// matches; it runs with -fno-eval-calls, so that the calls run.

static u32 counter;
static u64 wide = 0x0123456789abcdefu64;
static f64 scale = 2.5;
global u16 shared_half = 7u16;

// Managed code only writes this object; a naked body reads it.
static u32 mailbox;

[[noinline]] global void post(in u32 value) { mailbox = value; }

[[naked, clobber("rax")]]
global u32 read_mailbox() -> "eax" {
    register u32 result "eax";
    result = mailbox;
    $::_ret();
}

[[naked, clobber("rax")]]
global u32 *counter_address() -> "rax" {
    register u32 *result "rax";
    result = &counter;
    $::_ret();
}

[[naked, clobber("rax", "flags")]]
global void bump(in u32 amount "ecx") {
    counter = counter + amount;
    $::_ret();
}

[[raw_inline]]
static u64 mixed(in u64 value) { return value ^ wide; }

[[naked, clobber("r10", "r11", "flags")]]
global void mix_wide(inout u64 value "rax") {
    value = mixed(value);
    $::_ret();
}

[[naked, clobber("xmm1")]]
global f64 scaled(in f64 x "xmm0") -> "xmm0" {
    register f64 result "xmm0";
    result = x * scale;
    $::_ret();
}

[[naked, clobber("rax")]]
global u16 read_half() -> "ax" {
    register u16 result "ax";
    result = shared_half;
    $::_ret();
}

global u32 test_entry() {
    post(41u32);
    if (read_mailbox() != 41u32) return 1u32;
    if (counter_address() != &counter) return 2u32;
    *counter_address() = 5u32;
    bump(3u32);
    if (counter != 8u32) return 3u32;
    u64 value = 0xffu64;
    mix_wide(value);
    if (value != (0xffu64 ^ 0x0123456789abcdefu64)) return 4u32;
    if (scaled(4.0) != 10.0) return 5u32;
    shared_half = 9u16;
    if (read_half() != 9u16) return 6u32;
    return 0u32;
}
