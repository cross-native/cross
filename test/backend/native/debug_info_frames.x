// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Frames whose call-frame information changes inside the function.
[[noinline]] global i32 sink(i32 v) {
    return v + 1;
}

// Shrink-wrapped: the early return runs without the frame.
global i32 early(i32 v) {
    if (v < 0) {
        return 0;
    }
    return sink(v) + sink(v + 1);
}

// A sibling call leaves through an epilogue in the middle of the function.
global i32 relay(i32 v) {
    i32 r = sink(v);
    return sink(r + 1);
}

// A variable-length array keeps the CFA in $fp.
global i32 dynamic(u32 n) {
    stack u8 buffer[n];
    buffer[0] = 1u8;
    return sink((i32)buffer[0]);
}

struct wide_cell [[aligned(64)]] {
    i32 value;
};

// An over-aligned local realigns $fp; the CFA stays on $sp.
global i32 aligned(i32 v) {
    struct wide_cell cell;
    cell.value = v;
    return sink(cell.value);
}

[[noinline]] global i32 peek(i32 *cell) {
    return *cell;
}

// Shrink-wrapped with a local in the frame, which is described from the CFA.
global i32 shrunk(i32 v) {
    if (v < 0) {
        return 0;
    }
    i32 cell = v;
    return peek(&cell);
}
