// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("cross"), link_name("cross_link_sum9")]]
global u64 cross_link_sum9(in u64 a, in u64 b, in u64 c,
                           in u64 d, in u64 e, in u64 f,
                           in u64 g, in u64 h, in u64 i);

[[runtime_only, noinline, link_name("cross_link_entry")]]
global i32 cross_link_entry() {
    return $::runtime(cross_link_sum9(1, 2, 3, 4, 5, 6, 7, 8, 9) == 45);
}
