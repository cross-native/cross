// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 far_callee(in u32 value);

global u32 two_calls(in u32 value) {
    return far_callee(value) + far_callee(value + 1u32);
}

[[noinline]] static u32 helper(in u32 value) { return value * 3u32; }

global u32 static_call(in u32 value) { return helper(value) + 1u32; }

global u32 tail_call(in u32 value) { return far_callee(value + 2u32); }
