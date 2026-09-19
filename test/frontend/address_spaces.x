// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef volatile u32 [[address_space(0)]] *space_zero_ptr;

[[link_name("address_space_entry")]]
global i32 address_space_entry() {
    u32 value = 17u32;
    u32 [[address_space(0)]] *first = &value;
    u32 * [[address_space(0)]] second = first;
    space_zero_ptr third = second;
    if (sizeof(space_zero_ptr) != sizeof(u32 *)) return 2;
    *second = 29u32;
    return *third == 29u32 && first == second ? 1 : 3;
}
