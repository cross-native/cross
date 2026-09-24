// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("odd_abi")]] T odd_forward<T>(in T value);

global i32 generic_group_custom_entry() {
    return odd_forward<i32>(34i32) == 41i32 ? 1i32 : 2i32;
}
