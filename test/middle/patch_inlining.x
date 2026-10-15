// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Required inlining may copy a sinkless $::patch when that leaves exactly one
// emitted identity; copies in one function share its cell. Each error case
// leaves the identity in two functions.

#if defined(TWO_CALLERS)
[[always_inline]] static u32 patched() { return $::patch(7u32); }
global u32 first(in u32 value) { return patched() + value; }
global u32 second(in u32 value) { return patched() * value; }
#elif defined(OUT_OF_LINE)
[[always_inline]] global u32 patched() { return $::patch(7u32); }
global u32 entry(in u32 value) { return patched() + value; }
#elif defined(ADDRESS)
[[always_inline]] static u32 patched() { return $::patch(7u32); }
global u32 (*pointer)() = patched;
global u32 entry(in u32 value) { return patched() + value; }
#elif defined(NESTED)
[[always_inline]] static u32 inner() { return $::patch(7u32); }
[[always_inline]] static u32 outer() { return inner() + 1u32; }
global u32 first(in u32 value) { return outer() + value; }
global u32 second(in u32 value) { return outer() * value; }
#else
[[always_inline]] static u32 inner() { return $::patch(7u32); }
[[always_inline]] static u32 outer() { return inner() + 1u32; }
[[always_inline]] static u64 wide() { return $::patch(0x1122334455667788u64); }
// Discarded with its copy: nothing calls it.
static u32 unused(in u32 value) { return inner() - value; }

global u32 test_entry() {
    if (inner() != 7u32) return 1u32;
    if (outer() + inner() != 15u32) return 2u32;
    if (wide() != 0x1122334455667788u64) return 3u32;
    return 0u32;
}
#endif
