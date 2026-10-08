// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 target[4];
global u64 *first = &target[0];
global u64 *third = target + (1 + 1);
global uptr address = (uptr)&target;
global uptr address_third = (uptr)&target[0] + sizeof(u64) * 2;
global uptr address_before = (uptr)&target - sizeof(u64);
global uptr address_negative = (uptr)&target + -8iptr;
global u64 *signed_third = &target[3] + (i8)-1i32;
global u64 *index_third = &target[(i8)-1i32 + 3i32];
global u64 *cast_third = (u64 *)target + 2;
global u64 *pointer_table[2] = { &target[1], target + 3 };
global uptr integer_table[2] = { (uptr)&target, (uptr)&target[3] };

struct pair { u8 tag; u32 member; };
global struct pair object = { .tag = 7u8, .member = 9u32 };
global u32 *member_address = &object.member;

struct holder { u8 tag; u32 values[3]; };
global struct holder nested;
global u32 *nested_last = &nested.values[2];
global u32 *nested_second = nested.values + 1;
global u32 matrix[2][3];
global u32 *row_second = matrix[1] + 1;
global u32 *dereferenced_row = *(matrix + 1) + 1;
global u32 *cast_row = *((u32 (*)[3])matrix) + 1;
global u32 *cast_index = &(*((u32 (*)[3])matrix))[1];

namespace box {
    global u64 local[2];
    global uptr local_address = (uptr)local + sizeof(u64);
}

global i32 relocatable_address_entry() {
    if (first != &target[0]) return 1;
    if (third != &target[2]) return 2;
    if (address != (uptr)&target) return 3;
    if (address_third != (uptr)&target[2]) return 4;
    if (address_before != (uptr)&target - sizeof(u64)) return 12;
    if (address_negative != (uptr)&target - 8uptr) return 13;
    if (signed_third != &target[2]) return 14;
    if (index_third != &target[2]) return 15;
    if (cast_third != (u64 *)target + 2) return 8;
    if (pointer_table[0] != &target[1] ||
        pointer_table[1] != &target[3]) return 9;
    if (integer_table[0] != (uptr)&target ||
        integer_table[1] != (uptr)&target[3]) return 10;
    if (member_address != &object.member) return 5;
    if (nested_last != &nested.values[2]) return 6;
    if (nested_second != &nested.values[1]) return 7;
    if (row_second != &matrix[1][1]) return 16;
    if (dereferenced_row != &matrix[1][1]) return 17;
    if (cast_row != &matrix[0][1]) return 18;
    if (cast_index != &matrix[0][1]) return 19;
    if (box::local_address != (uptr)&box::local[1]) return 11;
    return 0;
}
