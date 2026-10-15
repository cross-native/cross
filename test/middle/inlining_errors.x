// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Mandatory inlining that cannot be done, one case per macro.

#if defined(RECURSIVE)
[[always_inline]] static u32 countdown(in u32 value) {
    if (value == 0u32) return 0u32;
    return countdown(value - 1u32) + 1u32;
}
global u32 entry(in u32 value) { return countdown(value) + countdown(value + 1u32); }
#elif defined(MUTUAL)
[[always_inline]] static u32 ping(in u32 value);
[[always_inline]] static u32 pong(in u32 value) {
    if (value == 0u32) return 1u32;
    return ping(value - 1u32);
}
[[always_inline]] static u32 ping(in u32 value) {
    if (value == 0u32) return 0u32;
    return pong(value - 1u32);
}
global u32 entry(in u32 value) { return ping(value); }
#elif defined(VARIADIC)
[[always_inline]] static u32 first(in u32 count, ...) { return count; }
global u32 entry(in u32 value) { return first(value, 1u32, 2u32); }
#elif defined(PATCH)
[[always_inline]] static u32 patched() { return $::patch(7u32); }
global u32 entry(in u32 value) { return patched() + value; }
#elif defined(MUSTTAIL)
[[noinline]] static u32 target(in u32 value) { return value + 1u32; }
[[always_inline]] static u32 forward(in u32 value) {
    [[musttail]] return target(value);
}
global u32 entry(in u32 value) { return forward(value) * 2u32; }
#elif defined(LOCATED)
[[always_inline]] static u32 located(in u32 value "edi") { return value + 1u32; }
global u32 entry(in u32 value) { return located(value); }
#elif defined(MANUAL_OUTPUT)
[[always_inline]] static void manual(in u32 value, out u32 result) -> "eax" {
    result = value;
}
global u32 entry(in u32 value) {
    u32 result;
    manual(value, result);
    return result;
}
#elif defined(NAKED_VLA)
[[raw_inline]] static u32 sum(in u32 count) {
    u32 values[count];
    values[0] = count;
    return values[0];
}
[[naked, clobber("t0", "t1", "t2")]]
global u32 naked_sum(in u32 value "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = sum(value);
    $::_jr(link);
}
#endif
