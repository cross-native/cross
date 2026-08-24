// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("cross_default_seven")]]
global u64 cross_default_seven(in u64 a, in u64 b, in u64 c, in u64 d,
                               in u64 e, in u64 f, in u64 g) {
    return a + b + c + d + e + f + g;
}

[[runtime_only, noinline, abi("sysv_abi"), link_name("explicit_sysv")]]
global u64 explicit_sysv(in u64 value) {
    return value + 1;
}

[[runtime_only, noinline, abi("ms_abi"), link_name("explicit_ms")]]
global u64 explicit_ms(in u64 value) {
    return value + 2;
}
