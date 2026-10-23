// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Runs the record copy checks of test/backend/native/aggregate_copies.x on
// the virt machine, whose MMU is off, so that misaligned accesses fault:
// those of packed records are left out.

global i32 aggregate_copies_aligned_entry();

global u32 test_main() {
    const i32 status = aggregate_copies_aligned_entry();
    return status == 1 ? 0u32 : (u32)status;
}
