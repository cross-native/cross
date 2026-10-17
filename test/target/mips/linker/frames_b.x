// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A second object whose CIE matches frames_a.x's.

global u32 helper(in u32 x) {
    return x * 5u32 + 7u32;
}

global u32 second(in u32 x) {
    return helper(x) + helper(x + 1u32);
}
