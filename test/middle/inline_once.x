// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static u32 once_loop(in u32 value) {
    for (u32 i = 0u32; i < 7u32; ++i)
        value = (value * 33u32 + i) ^ (value >> 3u32);
    return value;
}

[[noinline]] static u32 once_clone_target(in u32 selector, in u32 value) {
    if (selector == 1u32) return value + 41u32;
    return value + 43u32;
}
u32 once_group(in u32 value) {
    return once_clone_target(1u32, value) + once_clone_target(value, value);
}

[[abi("x")]] static u32 once_registered(in u32 value) {
    return value * 13u32 + 3u32;
}

static u32 once_inner(in u32 value) { return value * 5u32 + 2u32; }
static u32 once_outer(in u32 value) { return once_inner(value) ^ 17u32; }

static u32 once_recursive(in u32 value) {
    if (value == 0u32) return 1u32;
    return once_recursive(value - 1u32) + value;
}

static u32 once_cycle_b(in u32 value);
static u32 once_cycle_a(in u32 value) {
    if (value == 0u32) return 3u32;
    return once_cycle_b(value - 1u32) + 1u32;
}
static u32 once_cycle_b(in u32 value) { return once_cycle_a(value) + 2u32; }

[[abi("x")]] static u32 once_addressed(in u32 value) { return value + 21u32; }
typedef u32 (*once_callback)(in u32) [[abi("x")]];
global once_callback once_pointer = once_addressed;

[[used]] static u32 once_used(in u32 value) { return value + 23u32; }
[[retain]] static u32 once_retained(in u32 value) { return value + 25u32; }
[[noinline]] static u32 once_forbidden(in u32 value) { return value + 27u32; }
global u32 once_exported(in u32 value) { return value + 29u32; }

[[abi("x"), link_name("once_alias_target")]]
static u32 once_aliased(in u32 value) { return value + 31u32; }
[[abi("x"), alias("once_alias_target")]] global u32 once_alias(in u32 value);

static u32 once_label_owner(in u32 value) { point: return value + 33u32; }
global label once_label = once_label_owner::point;
static u32 once_patched(in u32 value) { return value + $::patch(35u32); }

static u32 once_leaf(in u32 value) { return value * 11u32 + 1u32; }
[[always_inline]] static u32 once_wrapper(in u32 value) { return once_leaf(value); }

static u32 once_vla(in u32 count) {
    volatile u32 data[count];
    data[count - 1u32] = count + 37u32;
    return data[count - 1u32];
}

[[abi("x")]] static u32 once_tail_target(in u32 value) { return value + 39u32; }
[[abi("x")]] static u32 once_tail_owner(in u32 value) {
    [[musttail]] return once_tail_target(value);
}

// A fixed frame alone must not be merged into a caller with a tiny frame.
static u32 once_large_stack(in u32 value) {
    volatile u8 data[4096];
    data[value & 4095u32] = (u8)value;
    return (u32)data[value & 4095u32];
}

global u32 once_entry(in u32 value) {
    return once_loop(value) + once_group(value) + once_registered(value) +
        once_outer(value) + once_recursive(value & 3u32) + once_cycle_a(value & 3u32) +
        once_addressed(value) + once_pointer(value + 1u32) + once_used(value) +
        once_retained(value) + once_forbidden(value) + once_exported(value) +
        once_aliased(value) + once_alias(value + 1u32) + once_label_owner(value) +
        once_patched(value) + once_wrapper(value) + once_wrapper(value + 1u32) +
        once_vla((value & 3u32) + 1u32) + once_large_stack(value) + once_tail_owner(value);
}

global u32 test_entry() {
    if (once_entry(0u32) != 41344995u32) return 1u32;
    if (once_entry(1u32) != 4034965685u32) return 2u32;
    if (once_entry(37u32) != 4175009097u32) return 3u32;
    return 0u32;
}
