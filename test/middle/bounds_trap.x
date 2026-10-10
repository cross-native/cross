// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// -fbounds-trap checks subscripts of arrays with a known bound. bounds_ok
// stays in range, including the one-past address `&table[4]` and an
// unchecked function; every other entry traps.

global u32 table[4] = {1u32, 2u32, 3u32, 4u32};
struct holder { u32 values[3]; u16 grid[2][3]; };
global struct holder held = {{5u32, 6u32, 7u32}, {{1u16, 2u16, 3u16}, {4u16, 5u16, 6u16}}};

[[noinline]] global u32 read_table(in u32 i) { return table[i]; }
[[noinline]] global u32 read_signed(in i32 i) { return table[i]; }
[[noinline]] global u32 read_member(in u32 i) { return held.values[i]; }
[[noinline]] global u32 read_grid(in u32 i, in u32 j) { return (u32)held.grid[i][j]; }
[[noinline]] global void write_table(in u32 i, in u32 v) { table[i] = v; }
[[noinline]] global uptr element_offset(in u32 i) {
    return (uptr)&table[i] - (uptr)&table[0];
}
[[noinline]] global u32 read_vla(in u32 n, in u32 i) {
    u32 local[n];
    for (u32 k = 0u32; k < n; ++k) local[k] = k * 3u32;
    return local[i];
}
[[noinline]] global u32 sum_table() {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < 4u32; ++i) sum += table[i];
    return sum;
}
[[no_sanitize("bounds"), noinline]] global uptr unchecked_offset(in u32 i) {
    return (uptr)&table[i] - (uptr)&table[0];
}

global u32 bounds_ok() {
    if (read_table(3u32) != 4u32) return 1u32;
    if (read_signed(2i32) != 3u32) return 2u32;
    if (read_member(2u32) != 7u32) return 3u32;
    if (read_grid(1u32, 2u32) != 6u32) return 4u32;
    write_table(0u32, 9u32);
    if (table[0] != 9u32) return 5u32;
    if (element_offset(4u32) != 16uptr) return 6u32;
    if (read_vla(5u32, 4u32) != 12u32) return 7u32;
    if (sum_table() != 18u32) return 8u32;
    if (unchecked_offset(7u32) != 28uptr) return 9u32;
    return 0u32;
}
global u32 bounds_past() { return read_table(4u32); }
global u32 bounds_negative() { return read_signed(-1i32); }
global u32 bounds_member() { return read_member(3u32); }
global u32 bounds_inner() { return read_grid(0u32, 3u32); }
global u32 bounds_outer() { return read_grid(2u32, 0u32); }
global u32 bounds_write() {
    write_table(4u32, 1u32);
    return 0u32;
}
global u32 bounds_one_past() { return (u32)element_offset(5u32); }
global u32 bounds_vla() { return read_vla(5u32, 5u32); }

#if defined(CASE)
global u32 test_entry() { return CASE(); }
#endif
