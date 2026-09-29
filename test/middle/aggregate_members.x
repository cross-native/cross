// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct member_pair { u32 left; u32 right; };
struct member_nested { struct member_pair pair; u32 values[3]; u32 *pointer; };
struct member_packed [[packed]] { u8 tag; u64 value; };
union member_union { u64 integer; f64 floating; };
struct member_bits { i32 signed_value : 5; u32 unsigned_value : 7; };
struct member_grid { u32 values[2][3]; };

[[noinline]] static T member_pass<T>(in T value) { return value; }
[[noinline]] static T member_counted<T>(in T value, inout u32 count) {
    count += 1u32;
    return value;
}
[[noinline]] static u32 member_row(in const u32 *values) { return values[2]; }

[[noinline]] global u32 member_host_read(in struct member_pair pair) {
    return pair.left + 3u32 * pair.right;
}
[[noinline]] static u32 member_nested_read(in struct member_nested cell,
                                         in struct member_nested other) {
    const u32 *left = &cell.pair.left;
    const u32 *values = cell.values;
    // The pointed-to object is distinct from the copied parameter cell.
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
    if (member_discard(pair) != 499u32) return 3;
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
    if (member_pass(pair).left != 7u32 ||
        (1 == 1 ? member_pass(pair) : pair).right != 11u32 ||
        ((struct member_pair)member_pass(pair)).left != 7u32) return 8;
    if (member_pass(cell).pair.right != 11u32 ||
        member_pass(cell).values[2] != 9u32 ||
        member_pass(member_pass(cell).pair).left != 7u32) return 9;
    if (member_pass(packed).value != 0x1122334455667788u64 ||
        member_pass(bits).integer != 0x123456789abcdef0u64) return 10;
    struct member_bits fields = {-7, 91u32};
    if (member_pass(fields).signed_value != -7 ||
        member_pass(fields).unsigned_value != 91u32) return 11;
    u32 count = 0u32;
    if (member_counted(cell, count).pointer[0] != 21u32 || count != 1u32) return 12;
    count = 0u32;
    if (member_counted(&cell, count)->pointer[0] != 21u32 || count != 1u32) return 13;
    count = 0u32;
    struct member_grid grid = {{{1u32, 2u32, 3u32}, {5u32, 7u32, 11u32}}};
    if (member_row(member_counted(grid, count).values[1]) != 11u32 || count != 1u32) return 14;
    count = 0u32;
    if (sizeof(member_counted(grid, count).values) != 6uptr * sizeof(u32) ||
        $::alignof(member_counted(grid, count).values) != $::alignof(u32) || count != 0u32) return 15;
    u32 sum = 0u32;
    for (u32 index = 0u32; index < 5u32; ++index)
        sum += member_counted(pair, count).right;
    if (sum != 55u32 || count != 5u32) return 16;
    return 1;
}
