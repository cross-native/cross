// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The partner of abicalls_runtime.x: global and thread-local objects, a
// function that calls back through a pointer, a tail call into that unit,
// and accesses to its thread-local object.

global u32 partner_value = 7u32;
global u32 partner_table[4] = {10u32, 20u32, 30u32, 40u32};
[[thread_local]] global u32 partner_tls = 9u32;
[[thread_local]] u32 tls_counter;
u32 twice(u32 x);

global u32 partner_add(u32 a, u32 b) { return a + b + partner_value; }
global u32 partner_apply(u32 (*f)(u32 x), u32 x) { return f(x) + 1u32; }
global u32 partner_twice(u32 x) { return twice(x); }

global u32 partner_tls_bump(u32 step) {
    tls_counter = tls_counter + step;
    partner_tls = partner_tls + step;
    return partner_tls;
}
