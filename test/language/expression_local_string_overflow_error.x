// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i32 local_string_overflow() {
    u8 too_small[2] = "abc";
    return too_small[0];
}
