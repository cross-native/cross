// Floating operations that may trap stay where the source executes them.

[[noinline]]
global f32 guarded_div(in f32 a, in f32 b) {
    return b != 0.0f32 ? a / b : 0.0f32;
}

// d may be zero only when n is.
[[noinline]]
global f32 guarded_loop(in const f32 *v, in u32 n, in f32 d) {
    f32 sum = 0.0f32;
    for (u32 i = 0u32; i < n; ++i) {
        sum += v[i] / d;
    }
    return sum;
}

// `!x` is loop invariant but runs only when the body does.
[[noinline]]
global u32 count_zero(in u32 n, in f32 x) {
    u32 count = 0u32;
    for (u32 i = 0u32; i < n; ++i) {
        if (!x) count += i;
    }
    return count;
}

[[noinline]]
global f32 sum(in const f32 *v, in u32 n) {
    f32 total = 0.0f32;
    for (u32 i = 0u32; i < n; ++i) {
        total += v[i];
    }
    return total;
}

[[noinline]]
global f32 fused(in f32 a, in f32 b, in f32 c) {
    return a * b + c;
}
