// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[weak, link_name("cross_weak_function")]]
global u32 weak_function() {
    return 23u32;
}

[[weak, link_name("cross_weak_object")]]
global u32 weak_object = 29u32;
