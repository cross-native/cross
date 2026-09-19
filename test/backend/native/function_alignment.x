// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[aligned(1u32 << 6u32), link_name("aligned_64")]]
global u32 aligned_64() {
    return 64u32;
}

[[aligned(1u32 << 5u32), aligned(sizeof(u128) << 3u32),
  link_name("aligned_128")]]
global u32 aligned_128() {
    return aligned_64() + 64u32;
}

[[link_name("aligned_plain")]]
global u32 aligned_plain() {
    return 1u32;
}
