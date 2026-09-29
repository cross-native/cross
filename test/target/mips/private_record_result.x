// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct medium { u32 values[6]; };
struct large { u32 values[12]; };
[[noinline]] static T pass<T>(in T value) { return value; }
[[abi("custom_record_result"), noinline]] static u32 fixed_scalar(in u32 value) {
    return value + 1u32;
}
[[abi("o32")]] global u32 private_record_entry() {
    struct medium first = {{1u32, 2u32, 3u32, 5u32, 7u32, 11u32}};
    struct large second = {{13u32, 17u32, 19u32, 23u32, 29u32, 31u32,
                            37u32, 41u32, 43u32, 47u32, 53u32, 59u32}};
    struct medium copied_first = pass(first);
    struct large copied_second = pass(second);
    if (copied_first.values[0] != 1u32 || copied_first.values[5] != 11u32 ||
        copied_second.values[0] != 13u32 || copied_second.values[11] != 59u32) return 0u32;
    if (pass(first).values[3] != 5u32 || pass(second).values[8] != 43u32) return 0u32;
    return fixed_scalar(60u32);
}
