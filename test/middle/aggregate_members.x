// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct member_pair { u32 left; u32 right; };
struct member_nested { struct member_pair pair; u32 values[3]; u32 *pointer; };
struct member_packed [[packed]] { u8 tag; u64 value; };
union member_union { u64 integer; f64 floating; };

[[noinline]] global u32 member_host_read(in struct member_pair pair) {
    return pair.left + 3u32 * pair.right;
}
[[noinline]] static u32 member_nested_read(in struct member_nested cell,
                                         in struct member_nested other) {
    const u32 *left = &cell.pair.left;
    const u32 *values = cell.values;
    // The pointer cell is immutable, not the separately owned pointee.
    cell.pointer[0] += 1u32;
    return *left + values[1] + cell.values[2] + other.pair.right +
        (&cell != &other);
}
[[noinline]] static u64 member_packed_read(in struct member_packed cell) {
    return cell.value + cell.tag;
}
[[noinline]] static u64 member_union_read(in union member_union cell) {
    return cell.integer;
}
[[noinline]] static void member_output(out struct member_pair cell) {
    cell.left = 99u32; cell.right = 100u32;
}
[[noinline]] static void member_update(inout struct member_pair cell) {
    cell.left += 100u32; cell.right += 200u32;
}
[[noinline]] static u32 member_discard(in struct member_pair cell) {
    member_output(cell);
    member_update(cell);
    return cell.left + cell.right;
}

global i32 aggregate_members_entry() {
    struct member_pair pair; pair.left = 7u32; pair.right = 11u32;
    if (member_host_read(pair) != 40u32) return 2;
    if (member_discard(pair) != 18u32) return 3;
    if (pair.left != 7u32 || pair.right != 11u32) return 4;
    u32 pointee = 20u32;
    struct member_nested cell; cell.pair = pair;
    cell.values[0] = 1u32; cell.values[1] = 5u32; cell.values[2] = 9u32;
    cell.pointer = &pointee;
    u32 nested = member_nested_read(cell, cell);
    if (nested != 33u32) return 100u32 + nested;
    if (pointee != 21u32) return 5;
    struct member_packed packed; packed.tag = 9u8;
    packed.value = 0x1122334455667788u64;
    if (member_packed_read(packed) != 0x1122334455667791u64) return 6;
    union member_union bits; bits.integer = 0x123456789abcdef0u64;
    if (member_union_read(bits) != 0x123456789abcdef0u64) return 7;
    return 1;
}
