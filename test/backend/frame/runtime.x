// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi")]] global u64 frame_touch(in u64 *values, in u64 a,
    in u64 b, in u64 c, in u64 d);

[[abi("ms_abi"), noinline]] global u64 frame_roundtrip(
    in u64 a, in u64 b, in u64 c, in u64 d, in u64 e) {
    u64 values[3];
    values[0] = a ^ e;
    values[1] = b + d;
    values[2] = c * 3u64;
    u64 first = frame_touch(values, a, b, c, e);
    if ((e & 1u64) != 0u64) {
        return first + values[0] + values[1] + values[2] + a + b + c + d + e;
    }
    u64 second = frame_touch(values, e, d, c, b);
    return (first ^ second) + values[0] + values[1] + values[2] + a + b + c + d + e;
}
