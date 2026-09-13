// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
[[abi("sysv_abi")]]
global i32 musttail_stack_callee(in i32 a, in i32 b, in i32 c, in i32 d,
                                 in i32 e, in i32 f, in i32 g) {
    return a + b + c + d + e + f + g;
}

[[abi("sysv_abi")]]
global i32 musttail_stack_caller(in i32 a, in i32 b, in i32 c, in i32 d,
                                 in i32 e, in i32 f, in i32 g) {
    [[musttail]] return musttail_stack_callee(a, b, c, d, e, f, g);
}
