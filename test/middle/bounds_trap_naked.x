// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// MIPS: -fbounds-trap checks the managed function but leaves the naked body
// exactly as written.

global u32 table[4];

[[naked, clobber("t0", "t1", "t2", "t3")]]
global u32 naked_load(in u32 index "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = table[index];
    $::_jr(link);
}

global u32 managed_load(in u32 index) { return table[index]; }
