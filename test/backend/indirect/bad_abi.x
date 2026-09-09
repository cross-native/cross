// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[abi("ms_abi")]] typedef u32 (*callback)(in u32 x);
[[abi("sysv_abi")]] u32 wrong(in u32 x) { return x; }
global u32 bad() {
    callback cb = wrong;
    return cb(1u32);
}
