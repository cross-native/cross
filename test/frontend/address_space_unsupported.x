// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 [[address_space(1)]] *unsupported_space;

global i32 cast_only_space() {
    u32 *ordinary = (u32 *)0uptr;
    return (i32)(uptr)((u32 [[address_space(2)]] *)ordinary);
}

global i32 evaluation_space() {
    return $::eval((i32)(uptr)((u32 [[address_space(3)]] *)0uptr));
}
