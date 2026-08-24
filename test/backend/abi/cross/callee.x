// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("cross_link_sum9")]]
global u64 cross_link_sum9(in u64 a, in u64 b, in u64 c,
                           in u64 d, in u64 e, in u64 f,
                           in u64 g, in u64 h, in u64 i) {
    return a + b + c + d + e + f + g + h + i;
}
