// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A function that is not `global` is emitted only when something emitted
// references it. GIMPLE leaves out the static data GCC GIMPLE cannot express.

typedef u32 (*unreferenced_step)(in u32);

[[always_inline]] static u32 inlined_step(in u32 value) {
    if (value > 100u32) return value;
    return value * 2u32 + 1u32;
}

static u32 called_only_by_unused(in u32 value) { return value ^ 0x55u32; }

[[noinline]] static u32 never_called(in u32 value) {
    return called_only_by_unused(value) + 1u32;
}

[[noinline]] u32 group_never_called(in u32 value) { return value - 1u32; }

[[used, noinline]] static u32 kept_used(in u32 value) { return value + 7u32; }

[[retain, noinline]] static u32 kept_retained(in u32 value) {
    return value + 9u32;
}

[[noinline]] static u32 kept_by_address(in u32 value) {
    return value + 13u32;
}

#if !defined(GIMPLE)
[[noinline]] static u32 kept_in_table(in u32 value) { return value + 11u32; }
static const unreferenced_step unreferenced_table[1] = { kept_in_table };

static void kept_label_owner() { mark: ; }
label unreferenced_label = kept_label_owner::mark;
#endif

global unreferenced_step unreferenced_pointer;

global u32 unreferenced_entry(in u32 value) {
    unreferenced_pointer = kept_by_address;
    return inlined_step(value) + inlined_step(value + 1u32);
}
