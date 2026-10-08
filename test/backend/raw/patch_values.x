// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static uptr patch_address;
static u64 initial() { return 0x1234567800000000u64; }

[[naked]] global void raw_values(out u64 wide "r10", out i64 negative "r11",
                                out uptr layout "r8") {
    $::_movabs(wide, ($::patch(initial() + (u64)0xabcdefu128, patch_address)));
    $::_movabs(negative, $::patch(-1i64));
    $::_movabs(layout, (($::patch((uptr)sizeof(u64)))));
    $::_ret();
}

[[naked]] static void raw_absolute(out uptr address "r9") {
    $::_movabs(address, $::patch((uptr)&*((u32*)0x1000uptr + 2u32)));
    $::_ret();
}

[[noinline]] static i32 check_values() {
    u64 wide;
    i64 negative;
    uptr layout;
    raw_values(wide, negative, layout);
    if (wide != 0x1234567800abcdefu64) return 1;
    if (negative != -1i64 || layout != sizeof(u64)) return 2;
    if (!patch_address || *((u64*)patch_address) != wide) return 3;
    uptr absolute;
    raw_absolute(absolute);
    if (absolute != 0x1008uptr) return 4;
    return 0;
}

[[abi(HOST_ABI), link_name("raw_patch_entry")]]
global i32 raw_patch_entry() { return check_values(); }
