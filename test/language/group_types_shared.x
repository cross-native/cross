// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Included by every group_types_*.x input of one compilation group.
#pragma once
namespace shape {
    enum kind [[underlying(u8)]] { circle = 1, square = 4 };
    struct point { i32 x; i32 y; };
}
typedef struct {
    union { i32 radius; i32 side; };
    struct shape::point origin;
    enum shape::kind kind;
} figure;
struct list<T> { T *items; uptr count; };
enum { LIMIT = 8 };
i32 area(const figure *f);
i32 total(struct list<figure> *all);
