// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Size presets derive a recurrence with a constant step from the loop's unit
// counter; the derived value is correct whether the loop reads it, stores
// it, or returns it. test_entry returns zero when every result matches. It
// runs with -fno-eval-calls, so that the calls run.

global u32 slots[16];

[[noinline]] global u32 tripled(in u32 count) {
    u32 total = 0u32;
    for (u32 at = 0u32; at < count; ++at) total += 3u32;
    return total;
}

[[noinline]] global void spaced(in u32 count) {
    u32 offset = 0u32;
    for (u32 at = 0u32; at < count; ++at) {
        slots[at] = offset + at;
        offset += 5u32;
    }
}

global u32 test_entry() {
    if ($::runtime(tripled(0u32)) != 0u32) return 1u32;
    if ($::runtime(tripled(7u32)) != 21u32) return 2u32;
    $::runtime(spaced(16u32));
    for (u32 at = 0u32; at < 16u32; ++at) {
        if (slots[at] != at * 6u32) return 3u32;
    }
    return 0u32;
}
