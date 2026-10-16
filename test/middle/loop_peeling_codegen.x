// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 table[8];
global u32 weights[4];

// The loop becomes straight-line code, whose reads of the weights take the
// values stored before it.
global u32 weighted() {
    weights[0] = 3u32;
    weights[1] = 5u32;
    weights[2] = 7u32;
    weights[3] = 11u32;
    u32 total = 0u32;
    for (u32 i = 0u32; i < 4u32; ++i) total = (total ^ table[i]) * weights[i];
    return total;
}
