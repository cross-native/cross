// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

enum expression_code [[underlying(u32)]] {
    expression_zero,
    expression_answer = 40u32,
    expression_next,
};

global enum expression_code expression_global = expression_next;
global uptr expression_layout = sizeof(u64[3]) + $::alignof(u64);
global const u8 expression_text[] = "ab" "\0" "cd";

$::static_assert(sizeof(u64) == 8uptr, "u64 size");
$::static_assert($::alignof(u64) == 8uptr, "u64 alignment");
$::static_assert(expression_next == 41u32, "enumerator value");
$::static_assert(sizeof(expression_text) == 6uptr, "inferred string array");
$::static_assert(sizeof("x" "yz") == 4uptr, "string expression size");

static i32 expression_static_counter() {
    static i32 counter = 3i32 + 4i32;
    counter += 1i32;
    return counter;
}

global i32 expression_core_entry() {
    i64 wide = 0x10000012ci64;
    if ((i32)(wide) != 300i32) return 2;
    if ((u8)(300u32) != 44u8) return 3;

    u64 values[5];
    values[0] = 11u64;
    values[1] = 22u64;
    values[2] = 33u64;
    values[3] = 44u64;
    values[4] = 55u64;
    u64 *base = &values[0];
    if (*(base + 2iptr) != 33u64) return 4;
    if (*(3iptr + base) != 44u64) return 5;
    if (*((base + 4iptr) - 1iptr) != 44u64) return 6;
    if ((base + 4iptr) - (base + 1iptr) != 3iptr) return 7;
    u64 *walk = base;
    if (*(walk++) != 11u64 || *walk != 22u64) return 16;
    walk += 2iptr;
    if (*walk != 44u64) return 17;
    --walk;
    walk -= 1iptr;
    if (*walk != 22u64) return 18;

    u64 through_pointer = 3u64;
    u64 *pointer = &through_pointer;
    (*pointer) += 10u64;
    if (through_pointer != 13u64) return 30;
    ((*pointer)) -= 3u64;
    if (through_pointer != 10u64) return 31;
    (through_pointer) *= 2u64;
    if (through_pointer != 20u64) return 32;
    if (((*pointer) += 1u64) != 21u64 || through_pointer != 21u64) return 34;
    u64 indexed[1] = { 4u64 };
    (indexed[0]) += 5u64;
    if (indexed[0] != 9u64) return 33;
    u64 tracked[2] = { 2u64, 4u64 };
    u64 *cursor = &tracked[0];
    (*(cursor++)) += 5u64;
    if (tracked[0] != 7u64 || tracked[1] != 4u64 ||
        cursor != &tracked[1]) return 35;

    uptr address = (uptr)(base);
    if ((u64 *)(address) != base) return 8;
    if ((bool)(base) != 1) return 22;
    u64 *null_pointer = (u64 *)0uptr;
    if (!null_pointer != 1) return 26;
    if (!base != 0) return 27;
    if (!!base != 1 || !!null_pointer != 0) return 28;
    if (!expression_static_counter != 0) return 29;
    if (sizeof(u64) != 8uptr) return 9;
    if ($::alignof(u64) != 8uptr) return 14;
    if (sizeof(values) != 40uptr) return 10;
    i32 unevaluated = 17i32;
    if (sizeof(unevaluated++) != 4uptr || unevaluated != 17i32) return 11;
    if (expression_answer != 40u32 || expression_next != 41u32) return 12;
    if (expression_global != expression_next) return 13;
    if (expression_layout != 32uptr) return 15;
    if (expression_static_counter() != 8i32 ||
        expression_static_counter() != 9i32) return 19;
    if (expression_text[0] != 97u8 || expression_text[1] != 98u8 ||
        expression_text[2] != 0u8 || expression_text[3] != 99u8 ||
        expression_text[4] != 100u8 || expression_text[5] != 0u8) return 20;
    const u8 *literal = "x" "yz";
    if (literal[0] != 120u8 || literal[1] != 121u8 ||
        literal[2] != 122u8 || literal[3] != 0u8) return 21;
    u8 automatic[4] = "abc";
    const u8 inferred[] = "q" "r";
    u8 padded[40] = "z";
    if (automatic[0] != 97u8 || automatic[1] != 98u8 ||
        automatic[2] != 99u8 || automatic[3] != 0u8) return 23;
    if (sizeof(inferred) != 3uptr || inferred[0] != 113u8 ||
        inferred[1] != 114u8 || inferred[2] != 0u8) return 24;
    if (padded[0] != 122u8 || padded[1] != 0u8 ||
        padded[17] != 0u8 || padded[39] != 0u8) return 25;
    return 1;
}
