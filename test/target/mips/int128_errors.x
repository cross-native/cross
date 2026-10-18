// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 128-bit uses that MIPS diagnoses: atomics (LL/SC is 32-bit), naked
// interfaces (one register cannot carry the value), and patch values.

#if CASE == 0
global u128 [[atomic]] cell;

global u128 load_cell() {
    return $::atomic_load(&cell, $::memory::seq_cst);
}
#elif CASE == 1
[[naked, clobber()]]
global u128 naked_wide(in u128 value "a0") -> "v0" {
    register u32 link "ra";
    $::_jr(link);
}
#else
global u128 patched() {
    return $::patch(5u128);
}
#endif
