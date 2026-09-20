// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[eval_only]]
static f32 step(in f32 value) {
    f32 result = value;
    result += 0.25f32;
    return result;
}

global f32 tie_even = 1.0f32 + 0x1p-24f32;
global f32 tie_up = 1.0f32 + 0x1.8p-24f32;
global f32 third = 1.0f32 / 3.0f32;
global f32 underflow = 0x1p-149f32 / 2.0f32;
global f32 signed_zero = -0.0f32;
global f32 through_call = step(1.25f32);
global f32 aggregate[2] = { 1.0f32 + 0.5f32, step(2.0f32) };
global f64 mixed_integer = 9007199254740992u64 + 1.0f64;
global f64 layout_sum = (f64)sizeof(f80) + 0.5f64;
global f64 layout_aggregate[1] = { (f64)sizeof(f80) + 0.5f64 };
global i32 truncated_float = (i32)-1.75f64;
global f32 rounded_integer = (f32)16777217u32;
global f32 positive_infinity = 1.0f32 / 0.0f32;
global f32 quiet_nan = (1.0f32 / 0.0f32) - (1.0f32 / 0.0f32);
global f80 extended_sum = 1.5f80 + 2.25f80;
global f128 wide_sum = 1.0f128 + 0x1p-112f128;

$::static_assert(1.5f32 > 1.0f32, "floating constants are scalar");
$::static_assert(-0.0f32 == 0.0f32, "signed zeros compare equal");
$::static_assert(
    (-0x1.cd9111e1916b48fbba82e0d0c0aap-128f128) *
        (0x1.83571d3dde620babbfeef6d1457cp-12f128) ==
        (-0x1.5d2faf870704ff8c4041c8f96e6ep-139f128),
    "binary128 multiplication rounds once");
$::static_assert(
    (-0x1.cd9111e1916b48fbba82e0d0c0aap-128f128) /
        (0x1.83571d3dde620babbfeef6d1457cp-12f128) ==
        (-0x1.310eb9e6dc8a7093e51b334f00d9p-116f128),
    "binary128 division rounds once");

global i32 floating_evaluation_entry() {
    f32 local = $::eval(step(0.75f32));
    return tie_even == 1.0f32 && tie_up > tie_even &&
        third > 0.3333333f32 && underflow == 0.0f32 &&
        signed_zero == 0.0f32 && through_call == 1.5f32 &&
        aggregate[0] == 1.5f32 && aggregate[1] == 2.25f32 &&
        mixed_integer == 9007199254740992.0f64 &&
        truncated_float == -1 && rounded_integer == 16777216.0f32 &&
        positive_infinity > 1.0f32 && quiet_nan != quiet_nan &&
        extended_sum == 3.75f80 &&
        wide_sum > 1.0f128 && local == 1.0f32;
}
