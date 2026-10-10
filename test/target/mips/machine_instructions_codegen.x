// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Assembly checks for MIPS machine-instruction built-ins: effects, purity,
// and coprocessor 0 hazard spacing.

// Count changes on its own: both reads stay.
global u32 count_reads() {
    u32 first;
    u32 second;
    $::_mfc0(first, 9);
    $::_mfc0(second, 9);
    return second - first;
}

// A pure form inlines and merges like a language operation.
static f32 root(in f32 value) {
    f32 result;
    $::_sqrt(result, value);
    return result;
}

global f32 inline_root(in f32 value) { return root(value) + root(value); }

// Cache maintenance is ordered with memory: the store precedes it and the
// load after it is not forwarded.
global u32 invalidate(in u32 *line) {
    line[0] = 5;
    $::_cache(0x11, line[0]);
    return line[0];
}

global u32 synced(in u32 *line) {
    line[1] = 7;
    $::_sync();
    return line[1];
}

// Status reaches interrupts, coprocessor usability, and ERET late.
global void set_status(in u32 status) { $::_mtc0(status, 12); }

// A TLB write reaches loads three instructions later.
global u32 after_tlb(in u32 *pointer) {
    $::_tlbwi();
    return *pointer;
}

// EPC must settle before ERET, Status before ERET.
[[naked, clobber("k0")]]
global void resume(in void *target "a0") {
    register u32 status "k0";
    $::_mtc0(target, 14);
    $::_mfc0(status, 12);
    status = status | 2u32;
    $::_mtc0(status, 12);
    $::_eret();
}

// A naked leaf computes into its result register and returns through ra.
[[naked]]
global u32 naked_leaf(in u32 base "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = base + 42u32;
    $::_jr(link);
}

// Only k0/k1 are free in an exception vector.
global void exception_handler();

[[naked, clobber("k0", "k1")]]
global void exception_vector() {
    register void (*handler)() "k0" = &exception_handler;
    $::_jr(handler);
}
