// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi")]]
global i32 musttail_home_callee(in i32 value);

[[abi("sysv_abi")]]
global i32 musttail_home_caller(in i32 value) {
    [[musttail]] return musttail_home_callee(value);
}
