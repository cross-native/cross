// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Functions with unwind tables for eh_frame.cmake; `unused` is collected
// under --gc-sections.

global u32 helper(in u32 x);

global u32 first(in u32 x) {
    return helper(x) + 1u32;
}

global u32 unused(in u32 x) {
    return helper(x * 2u32) + 3u32;
}
