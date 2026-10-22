// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "group_types_shared.x"

i32 total(struct list<figure> *all) {
    i32 sum = 0;
    for (uptr i = 0; i < all->count; ++i) sum += area(&all->items[i]);
    return sum;
}

global i32 group_three(figure *items, uptr count) {
    struct list<figure> all = { items, count };
    return total(&all) + (i32)sizeof(figure) + LIMIT;
}
