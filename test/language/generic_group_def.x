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

[[generic(label Address), noinline]] label group_label() { return Address; }
static void private_label_owner() { point: ; }
global label group_other_label() { return group_label::<private_label_owner::point>(); }
global label group_other_direct() { return private_label_owner::point; }
