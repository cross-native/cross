// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct large_aligned_frame [[aligned(64)]] {
    u8 bytes[12288];
};

[[runtime_only, noinline]]
static i32 probe_plain_static_frame() {
    u8 bytes[12288];
    bytes[0] = 1u8;
    bytes[4096] = 2u8;
    bytes[8192] = 3u8;
    bytes[12287] = 4u8;
    return bytes[0] + bytes[4096] + bytes[8192] + bytes[12287];
}

[[runtime_only, noinline]]
static i32 probe_aligned_static_frame() {
    struct large_aligned_frame frame;
    frame.bytes[0] = 5u8;
    frame.bytes[4096] = 6u8;
    frame.bytes[8192] = 7u8;
    frame.bytes[12287] = 8u8;
    return frame.bytes[0] + frame.bytes[4096] +
           frame.bytes[8192] + frame.bytes[12287];
}

[[runtime_only, noinline]]
static i32 probe_manual_input_frame(in i32 left "r10d",
                                    in i32 right "r11d") {
    u8 bytes[8192];
    bytes[0] = 1u8;
    bytes[8191] = 2u8;
    return left + right + bytes[0] + bytes[8191];
}

global i32 static_stack_probe_entry() {
    return probe_plain_static_frame() + probe_aligned_static_frame() +
           probe_manual_input_frame(9, 10);
}
