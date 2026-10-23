// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// test_main for a shared test whose entry is test_entry, which returns 0
// on success.

global i32 test_entry();

global u32 test_main() {
    return (u32)test_entry();
}
