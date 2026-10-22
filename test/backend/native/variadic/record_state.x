// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The record of a user ABI's record_address state (record_state.xm): its
// fields hold the values that the states' own bindings receive, at the
// offsets of a record of their types, and it has the state's alignment.

[[abi("record-test"), noinline,
  variadic(void *record "record", u8 count "count", u64 *saved "saved",
           u64 *incoming "stack")]]
static u32 walk(in u64 tag, ...) {
    const u8 *bytes = (const u8 *)record;
    u32 failures = 0u32;
    if (((uptr)record & 15uptr) != 0uptr) failures |= 1u32;
    if (bytes[0] != count || count != 1u8) failures |= 2u32;
    if (*(u64 **)(bytes + 8) != saved || saved[0] != 11u64 || saved[1] != 12u64)
        failures |= 4u32;
    if (*(u64 **)(bytes + 16) != incoming || incoming[0] != 13u64 ||
        incoming[1] != 14u64)
        failures |= 8u32;
    return tag == 7u64 ? failures : 16u32;
}

global u32 record_entry() {
    return walk(7u64, 11u64, 12u64, 13u64, 14u64);
}
