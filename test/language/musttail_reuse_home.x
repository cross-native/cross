// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi"), noinline]]
global i32 musttail_reuse_home_callee(in i32 value) {
    return value + 17i32;
}

// Four SysV stack arguments reserve 32 incoming bytes.  The MS callee may
// reuse them as its home area after the tail transfer.
[[abi("sysv_abi"), noinline]]
global i32 musttail_reuse_home_caller(
    in i32 a, in i32 b, in i32 c, in i32 d, in i32 e,
    in i32 f, in i32 g, in i32 h, in i32 i, in i32 j) {
    [[musttail]] return musttail_reuse_home_callee(a);
}

[[link_name("musttail_reuse_home_entry")]]
global i32 musttail_reuse_home_entry() {
    return musttail_reuse_home_caller(
        25i32, 1i32, 2i32, 3i32, 4i32,
        5i32, 6i32, 7i32, 8i32, 9i32) == 42i32;
}
