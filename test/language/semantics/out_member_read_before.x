// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct pair { i32 first; i32 second; };
global i32 invalid(out struct pair value) {
    value.first = 3;
    i32 result = value.second;
    value.second = 4;
    return result;
}
