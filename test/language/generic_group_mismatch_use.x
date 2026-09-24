// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

T mismatch<T>(in T value);
global i32 generic_group_mismatch_entry() {
    return mismatch<i32>(1i32);
}
