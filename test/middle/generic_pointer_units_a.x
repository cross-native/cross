// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[generic(const u32 *P), noinline]]
global const u32 *shared_pointer() { return P; }
typedef u32 (*UnitCallback)(in u32 x);
[[generic(UnitCallback F), noinline]]
global u32 shared_callback(in u32 x) { return F(x); }

static u32 cell = 7u32;
static const u32 *local_address() { return &cell; }
[[generic(const u32 *P), noinline]]
static u32 private_read() { return *P + 100u32; }
[[abi(HOST_ABI), noinline]]
static u32 callback(in u32 x) { return x + 10u32; }

[[abi(HOST_ABI), link_name("pointer_unit_a")]]
global i32 pointer_unit_a() {
    const u32 *p = shared_pointer::<local_address()>();
    return *p == 7u32 && private_read::<&cell>() == 107u32 &&
           shared_callback::<callback>(1u32) == 11u32;
}
