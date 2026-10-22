// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "group_types_shared.x"

i32 area(const figure *f) {
    return f->kind == shape::square ? f->side * f->side : 3 * f->radius * f->radius;
}
