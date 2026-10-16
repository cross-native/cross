// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Stores to fixed locations that leave their loops for the loop exits keep
// the last value stored, and a loop that stores nothing leaves memory as it
// was. Calls, stores through pointers that may alias, and volatile accesses
// in a loop keep its stores. Exits taken before a store, every scalar width,
// floating values, record members, local arrays, and nested loops are
// covered. test_entry returns zero when every result matches. It runs with
// -fno-eval-calls, so that the calls run.

struct pair { u32 first; u32 second; };

global u32 last;
global u32 zero_trip;
global u32 called;
global u32 observed;
global u32 aliased;
global volatile u32 device;
global u32 beside_device;
global u32 multiple;
global u32 early;
global u8 narrow;
global u16 half;
global u64 wide;
global f32 single;
global f64 double_value;
global u32 *pointer;
global u32 cells[8];
global struct pair members;
global u32 accumulated;
global u32 outer;
global u32 inner;
static u32 write_only;

[[noinline]] global u32 sum_into_last(in u32 n) {
    u32 sum = 0u32;
    for (u32 k = 0u32; k < n; ++k) {
        sum = sum + k;
        last = sum * 3u32;
    }
    return sum;
}

[[noinline]] global void store_when_run(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) zero_trip = k * 5u32 + 1u32;
}

[[noinline]] global void observe() { observed = observed + called; }

[[noinline]] global void call_each(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        called = k + 1u32;
        observe();
    }
}

// p may point to aliased.
[[noinline]] global void through_pointer(in u32 *p, in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        aliased = k;
        *p = *p + 2u32;
    }
}

[[noinline]] global void beside_volatile(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        beside_device = k + 9u32;
        device = k;
    }
}

// Returns at the first k whose value is stop, else after n passes; both
// exits leave after the store.
[[noinline]] global u32 search(in u32 n, in u32 stop) {
    u32 k = 0u32;
    for (;;) {
        multiple = k * 7u32;
        if (k == stop) return 1u32;
        k = k + 1u32;
        if (k >= n) break;
    }
    return 0u32;
}

// The first exit leaves before the store, which therefore stays.
[[noinline]] global u32 leave_early(in u32 n, in u32 stop) {
    for (u32 k = 0u32; k < n; ++k) {
        if (k == stop) return k;
        early = k + 100u32;
    }
    return n;
}

[[noinline]] global void widths(in u32 n) {
    u64 state = 1u64;
    f32 x = 1.0f32;
    f64 y = 0.5f64;
    for (u32 k = 0u32; k < n; ++k) {
        state = state * 6364136223846793005u64 + 1442695040888963407u64;
        x = x * 1.5f32;
        y = y + 0.25f64;
        narrow = (u8)(k + 250u32);
        half = (u16)(k * 4099u32);
        wide = state;
        single = x;
        double_value = y;
        pointer = &cells[k & 7u32];
    }
}

[[noinline]] global u32 record_member(in u32 n) {
    u32 total = 0u32;
    for (u32 k = 0u32; k < n; ++k) {
        total = total + members.first;
        members.second = k * 11u32;
    }
    return total;
}

[[noinline]] global u32 local_cells(in u32 n) {
    u32 local[4] = {1u32, 2u32, 3u32, 4u32};
    u32 *view = local;
    for (u32 k = 0u32; k < n; ++k) local[2] = k * 13u32;
    return view[0] + view[2] * 10u32;
}

// Each pass reads what the previous one stored.
[[noinline]] global void accumulate(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) accumulated = accumulated + k;
}

[[noinline]] global void nested(in u32 n, in u32 m) {
    for (u32 i = 0u32; i < n; ++i) {
        for (u32 j = 0u32; j < m; ++j) inner = i * 100u32 + j;
        outer = i;
    }
}

// The object is never read, so it and its stores may disappear.
[[noinline]] global u32 only_written(in u32 n) {
    write_only = 1u32;
    u32 sum = 0u32;
    for (u32 k = 0u32; k < n; ++k) {
        const u32 a = write_only + k;
        write_only = a;
        sum = sum + a;
    }
    return sum;
}

global u32 test_entry() {
    if ($::runtime(sum_into_last(10u32)) != 45u32 || last != 135u32) return 1u32;
    zero_trip = 77u32;
    $::runtime(store_when_run(0u32));
    if (zero_trip != 77u32) return 2u32;
    $::runtime(store_when_run(4u32));
    if (zero_trip != 16u32) return 3u32;
    $::runtime(call_each(5u32));
    if (called != 5u32 || observed != 15u32) return 4u32;
    aliased = 0u32;
    $::runtime(through_pointer(&aliased, 4u32));
    if (aliased != 5u32) return 5u32;
    $::runtime(through_pointer(&observed, 3u32));
    if (aliased != 2u32 || observed != 21u32) return 6u32;
    $::runtime(beside_volatile(6u32));
    if (beside_device != 14u32 || device != 5u32) return 7u32;
    if ($::runtime(search(10u32, 3u32)) != 1u32 || multiple != 21u32) return 8u32;
    if ($::runtime(search(5u32, 9u32)) != 0u32 || multiple != 28u32) return 9u32;
    early = 5u32;
    if ($::runtime(leave_early(10u32, 0u32)) != 0u32 || early != 5u32) return 10u32;
    if ($::runtime(leave_early(10u32, 4u32)) != 4u32 || early != 103u32) return 11u32;
    if ($::runtime(leave_early(3u32, 9u32)) != 3u32 || early != 102u32) return 12u32;
    $::runtime(widths(10u32));
    if (narrow != 3u8 || half != 36891u16 || wide != 0x32ccf775fe645423u64 ||
        single != 57.6650390625f32 || double_value != 3.0f64 ||
        pointer != &cells[1]) return 13u32;
    members.first = 2u32;
    if ($::runtime(record_member(6u32)) != 12u32 || members.second != 55u32) return 14u32;
    if ($::runtime(local_cells(0u32)) != 31u32) return 15u32;
    if ($::runtime(local_cells(5u32)) != 521u32) return 16u32;
    accumulated = 3u32;
    $::runtime(accumulate(5u32));
    if (accumulated != 13u32) return 20u32;
    inner = 1u32;
    outer = 1u32;
    $::runtime(nested(3u32, 0u32));
    if (inner != 1u32 || outer != 2u32) return 17u32;
    $::runtime(nested(3u32, 4u32));
    if (inner != 203u32 || outer != 2u32) return 18u32;
    if ($::runtime(only_written(5u32)) != 25u32) return 19u32;
    return 0u32;
}
