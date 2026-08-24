// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 pointer_target = 0u64;

global u64 *pointer_in_simd(in u64 *value "xmm1") -> "xmm0" {
    return value;
}

global i32 pointer_simd_entry() {
    return pointer_in_simd(&pointer_target) == &pointer_target;
}
