// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline, runtime_only]]
u64 private_custom(in u64 value) {
    return value * 3u64 + 7u64;
}

[[abi("ms_abi"), noinline]]
global u64 dynamic_custom_entry(in u64 value) {
    return private_custom(value);
}
