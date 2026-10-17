// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Values that the allocator places in the floating result register stay
// intact across instructions that stage vectors through the emitter's
// XMM0-XMM2 temporaries: arithmetic and lane reads of vectors kept in their
// homes, variable lane reads, and multiply-adds. test_entry returns zero
// when every result is exact; it runs with -fno-eval-calls, so that the
// calls run.

typedef f64 f64x2 [[vector_size(16)]];
typedef f64 f64x8 [[vector_size(64)]];

[[noinline]] global f64 scale(in f64 x) {
    return x * 4.0;
}

[[noinline]] global f64x2 pair(in f64 first, in f64 second) {
    f64x2 v = first;
    v[1] = second;
    return v;
}

// A call result live across a lane read of a vector kept in its home.
[[noinline]] global f64 call_then_lane(in f64x2 w) {
    f64 r = scale(1.0);
    return r * w[1];
}

// A call result live across arithmetic on vectors kept in their homes.
[[noinline]] global f64 call_then_add(in f64x2 w, in f64x2 z) {
    f64 r = scale(1.0);
    f64x2 t = w + z;
    return r * t[0];
}

// A call result live across a variable lane read.
[[noinline]] global f64 call_then_index(in f64x2 w, in uptr k) {
    f64 r = scale(1.0);
    return r * w[k];
}

// A constant lane live across a variable lane read of the same vector.
[[noinline]] global f64 lane_then_index(in uptr k) {
    f64x2 v = pair(0.5, -3.0);
    return v[0] + v[k];
}

// A product live across arithmetic on vectors wider than the registers.
[[noinline]] global f64 wide(in f64x8 a, in f64x8 b, in f64 x, in f64 y) {
    f64 r = x * y;
    f64x8 s = a + b;
    return r * s[3];
}

// A call result that is the addend of a multiply-add.
[[noinline]] global f64 call_then_fused(in f64 a, in f64 b, in f64 c) {
    f64 m = b * c;
    f64 r = scale(a);
    return r + m;
}

global u32 test_entry() {
    const f64x2 w = pair(2.0, 3.0);
    const f64x2 z = pair(0.25, 5.0);
    if (call_then_lane(w) != 12.0) return 1u32;
    if (call_then_add(w, z) != 9.0) return 2u32;
    if (call_then_index(w, 0) != 8.0 || call_then_index(w, 1) != 12.0) {
        return 3u32;
    }
    if (lane_then_index(0) != 1.0 || lane_then_index(1) != -2.5) return 4u32;
    f64x8 a = 1.0;
    f64x8 b = 0.5;
    b[3] = 2.0;
    if (wide(a, b, 3.0, 0.5) != 4.5) return 5u32;
    if (call_then_fused(1.0, 2.0, 3.0) != 10.0) return 6u32;
    return 0u32;
}
