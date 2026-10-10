// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Each check returns 1 when the instruction behaves as on a VR4300.

static volatile u32 spin_counter;
global f32 single_inputs[3] = {2.25f32, -3.5f32, 1.25f32};
global f64 double_inputs[3] = {6.25, -0.5, 1.75};
static u32 cache_buffer[8];

global u32 count_advances() {
    u32 first;
    u32 second;
    $::_mfc0(first, 9);
    for (u32 index = 0; index < 20000; ++index) spin_counter = index;
    $::_mfc0(second, 9);
    return second != first;
}

global u32 single_forms() {
    f32 root;
    f32 magnitude;
    f32 negated;
    f32 square = single_inputs[0];
    f32 negative = single_inputs[1];
    f32 positive = single_inputs[2];
    $::_sqrt(root, square);
    $::_abs(magnitude, negative);
    $::_neg(negated, positive);
    return root == 1.5f32 && magnitude == 3.5f32 && negated == -1.25f32;
}

global u32 double_forms() {
    f64 root;
    f64 magnitude;
    f64 negated;
    f64 square = double_inputs[0];
    f64 negative = double_inputs[1];
    f64 positive = double_inputs[2];
    $::_sqrt(root, square);
    $::_abs(magnitude, negative);
    $::_neg(negated, positive);
    return root == 2.5 && magnitude == 0.5 && negated == -1.75;
}

global u32 compare_round_trip() {
    u32 written = 0x12345678;
    u32 read;
    $::_mtc0(written, 11);
    $::_mfc0(read, 11);
    return read == written;
}

global u32 fcsr_round_trip() {
    u32 saved;
    u32 changed;
    $::_cfc1(saved, 31);
    // Round toward zero.
    u32 toward_zero = (saved & ~3u32) | 1u32;
    $::_ctc1(toward_zero, 31);
    $::_cfc1(changed, 31);
    $::_ctc1(saved, 31);
    return (changed & 3u32) == 1u32;
}

global u32 tlb_round_trip() {
    u32 index = 5;
    u32 entry_hi = 0x00400000;
    u32 entry_lo0 = (0x00200000u32 >> 6) | 0x1fu32;
    u32 entry_lo1 = 0x1fu32;
    u32 page_mask = 0;
    u32 cleared = 0;
    u32 read_hi;
    u32 probed;
    $::_mtc0(index, 0);
    $::_mtc0(page_mask, 5);
    $::_mtc0(entry_hi, 10);
    $::_mtc0(entry_lo0, 2);
    $::_mtc0(entry_lo1, 3);
    $::_tlbwi();
    $::_mtc0(cleared, 10);
    $::_tlbr();
    $::_mfc0(read_hi, 10);
    $::_mtc0(entry_hi, 10);
    $::_tlbp();
    $::_mfc0(probed, 0);
    return read_hi == entry_hi && probed == index;
}

global u32 cache_writeback() {
    cache_buffer[3] = 0xcafe0003;
    $::_cache(0x19, cache_buffer[3]);
    $::_sync();
    return cache_buffer[3] == 0xcafe0003;
}

global u32 nop_form() {
    $::_nop();
    return 1;
}

// A naked leaf: its result register is a hard-bound object and it returns
// through the return-address register.
[[naked, clobber("t0")]]
global u32 naked_answer(in u32 base "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = base + 42u32;
    $::_jr(link);
}

[[raw_inline]]
static u32 scaled(in u32 value) { return (value << 2) + 1u32; }

[[naked, clobber("t0", "t1")]]
global u32 naked_scaled(in u32 value "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = scaled(value);
    $::_jr(link);
}

// Leaves through ERET to `target` with Status.EXL set and ERL clear.
[[naked, clobber("k0", "k1")]]
global void return_through_eret(in void *target "a0") {
    register u32 status "k0";
    register u32 mask "k1" = 0xfffffffbu32;
    $::_mtc0(target, 14);
    $::_mfc0(status, 12);
    status = (status & mask) | 2u32;
    $::_mtc0(status, 12);
    $::_eret();
}
