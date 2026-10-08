// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef CUSTOM_EVAL_ABI
#define EVAL_OUTPUT_ABI [[abi("stack_result_abi")]]
#else
#define EVAL_OUTPUT_ABI
#endif
EVAL_OUTPUT_ABI [[noinline]] static void initialize(out f80 first, out f80 second) {
    first = 1.25f80;
    second = 2.5f80;
}
EVAL_OUTPUT_ABI [[noinline]] static void increment(inout f80 first, inout f80 second) {
    first += 0.5f80;
    second += 1.25f80;
}
static u32 run(in u32 seed) {
    f80 first, second;
    initialize(first, second);
    if (first != 1.25f80 || second != 2.5f80) return 1u32;
    increment(first, second);
    if (first != 1.75f80 || second != 3.75f80) return 2u32;
    return seed == 3u32 ? 61u32 : 3u32;
}
$::static_assert($::eval(run(3u32)) == 61u32, "independent f80 output cells");
static volatile u32 seed = 3u32;
[[abi(HOST_ABI)]] global u32 syntax_raw_entry() { return run(seed); }
