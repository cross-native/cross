// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

T missing<T>(in T value);
global i32 generic_group_undefined_entry() {
    return missing<i32>(1i32);
}
