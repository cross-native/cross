// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static u32 cell = 13u32;
static const u32 *local_address() { return &cell; }
[[generic(const u32 *P), noinline]]
static u32 private_read() { return *P + 200u32; }
[[abi(HOST_ABI), noinline]]
static u32 callback(in u32 x) { return x + 20u32; }

[[abi(HOST_ABI), link_name("pointer_unit_b")]]
global i32 pointer_unit_b() {
    const u32 *p = shared_pointer::<local_address()>();
    return *p == 13u32 && private_read::<&cell>() == 213u32 &&
           shared_callback::<callback>(1u32) == 21u32;
}
