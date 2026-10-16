// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The partner of small_data_runtime.x: it reaches that unit's small data
// through declared sections and defines a small and a large object.

[[section(".sbss")]] u32 counter;
[[section(".sdata")]] u32 seed;
global u32 partner_small = 41u32;
global u32 partner_large[8] = {1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32};

global u32 partner_touch(u32 step) {
    counter = counter + step;
    return seed + partner_small + partner_large[2];
}
