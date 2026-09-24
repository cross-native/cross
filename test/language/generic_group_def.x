// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

T isolated<T>(in T value) {
    return value + 5i32;
}

namespace math {
    U choose<U>(in bool first, in U left, in U right) {
        return first ? left : right;
    }

    [[generic(V)]] V legacy(in V value) {
        return value;
    }
}
