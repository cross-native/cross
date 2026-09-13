// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("o32"), clobber("s0")]]
global i32 musttail_extra_clobber(in i32 value);

[[abi("o32")]]
global i32 musttail_narrow_caller(in i32 value) {
    [[musttail]] return musttail_extra_clobber(value);
}
