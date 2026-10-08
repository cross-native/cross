// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
typedef u32 U4 [[ext_vector_type(4)]];
global volatile U4 lanes;
global u32 entry(in uptr index) {
    lanes[index] = 3u32;
    lanes[index] += 5u32;
    return lanes[index]++;
}
