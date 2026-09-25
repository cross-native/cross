// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 i32x4 [[ext_vector_type(4)]];
typedef i32 i32x2 [[ext_vector_type(2)]];

static T same_shape<T>(in T left, in T right) { return left; }

global i32 bad() {
    i32x4 left = 1i32;
    i32x2 right = 2i32;
    return same_shape(left + right, left)[0];
}
