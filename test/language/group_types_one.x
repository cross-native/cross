// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "group_types_shared.x"

global i32 group_one(figure *f) {
    return area(f) + LIMIT;
}
