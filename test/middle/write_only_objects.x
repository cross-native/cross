// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An object that is not `global` and that no emitted code reads goes away
// with its stores, together with what only its initializer names; every kind
// of root stays.

typedef u32 (*write_only_step)(in u32);

static u32 removed_written;
static u32 removed_never_named;
u32 removed_group_written = 0u32;
static u32 removed_cells[8];
static const u32 removed_folded[3] = {4u32, 5u32, 6u32};
static u32 removed_target;
static u32 *removed_holder = &removed_target;
[[noinline]] static u32 removed_by_table(in u32 value) { return value + 3u32; }
static write_only_step removed_table[1] = { removed_by_table };

static u32 kept_escaped;
static u32 kept_loaded;
[[used]] static u32 kept_used;
[[retain]] static u32 kept_retained;
global u32 kept_exported;
static volatile u32 kept_volatile;
[[section(".data.write_only")]] static u32 kept_sectioned;
[[noinit]] static u32 kept_noinit;
static u32 [[atomic]] kept_atomic;
static u32 kept_target;
static u32 *kept_holder = &kept_target;

[[noinline]] global u32 write_only_entry(in u32 n, in u32 i) {
    removed_written = 1u32;
    u32 sum = 0u32;
    for (u32 k = 0u32; k < n; ++k) {
        const u32 a = removed_written + k;
        removed_written = a;
        sum = sum + a;
    }
    removed_group_written = sum;
    removed_cells[i & 7u32] = n;
    removed_cells[2] = i;
    kept_loaded = sum + removed_folded[1];
    kept_used = n;
    kept_retained = n;
    kept_exported = n;
    kept_volatile = n;
    kept_sectioned = n;
    kept_noinit = n;
    kept_atomic = n;
    return sum;
}

[[noinline]] global u32 *write_only_escape() {
    kept_escaped = 2u32;
    return &kept_escaped;
}

[[noinline]] global u32 write_only_read() { return kept_loaded + *kept_holder; }
