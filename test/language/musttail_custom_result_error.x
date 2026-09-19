// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("odd_abi")]]
global u32 musttail_odd_result_callee(in u32 value);

[[abi("sysv_abi")]]
global u32 musttail_odd_result_caller(in u32 value) {
    [[musttail]] return musttail_odd_result_callee(value);
}
