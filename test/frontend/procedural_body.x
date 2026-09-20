// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]]
static $::meta::tokens add_five(in $::meta::tokens input) {
    $::meta::tokens copy = input;
    $::meta::tokens result = $::meta::concat(
        copy, $::meta::parse(" + 5u64"));
    return result;
}

[[macro]]
static $::meta::tokens choose(in $::meta::tokens input) {
    if (1) {
        $::meta::tokens copy = input;
        return $::quote { $::unquote(copy) };
    } else {
        return $::meta::parse("99u64");
    }
}

[[macro]]
static $::meta::tokens choose_else(in $::meta::tokens input) {
    if (0) {
        return $::meta::parse("99u64");
    } else {
        $::meta::tokens copy = input;
        return copy;
    }
}

global u64 procedural_body_entry() {
    return add_five! { 2u64 } + choose! { 9u64 } +
           choose_else! { 3u64 };
}
