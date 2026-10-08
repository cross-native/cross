// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
typedef u32 U4 [[ext_vector_type(4)]];
global u32 entry() {
    U4 lanes = 1u32;
#if MODE == 0
    lanes[4u32] += 3u32;
#elif MODE == 1
    ++lanes[4u32];
#elif MODE == 2
    lanes[-1i32]--;
#elif MODE == 3
    lanes[4u32] = 3u32;
#else
    return lanes[4u32];
#endif
    return lanes[0u32];
}
