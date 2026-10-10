// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An array bound that comes from the initializer is known to every layout
// query in the group, including array bounds and enumerators that the
// compiler resolves before it processes that initializer.

global u32 table[] = {10u32, 20u32, 30u32};
global u8 shadow[sizeof(table) / sizeof(table[0])];
enum counts { table_count = sizeof(table) / sizeof(table[0]) };
$::static_assert(sizeof(table) == 12uptr, "brace bound");
$::static_assert(sizeof(shadow) == 3uptr, "dependent bound");
$::static_assert((uptr)table_count == 3uptr, "dependent enumerator");

enum positions { last = 6 };
global u16 sparse[] = {1u16, [last] = 7u16, 8u16};
global u8 sparse_copy[sizeof(sparse)];
$::static_assert(sizeof(sparse_copy) == 16uptr, "designated bound");

global u8 before[sizeof(after)];
global u32 after[] = {1u32, 2u32};
$::static_assert(sizeof(before) == 8uptr, "later definition");

global u8 text[] = "abc";
global u8 text_copy[sizeof(text)];
$::static_assert(sizeof(text_copy) == 4uptr, "string bound");

global u8 asset[] = $::embed("inferred_bounds.txt");
global u8 asset_copy[sizeof(asset)];
$::static_assert(sizeof(asset_copy) == 5uptr, "embedded bound");

global uptr block_static_extent() {
    static u32 local[] = {1u32, 2u32, 3u32, 4u32};
    static u8 local_copy[sizeof(local)];
    $::static_assert(sizeof(local_copy) == 16uptr, "block static bound");
    return sizeof(local_copy);
}
