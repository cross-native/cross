// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens first_tree(in $::meta::tokens input) {
    if ($::meta::len(input) != 2u32) return $::quote { 99u32 };
    return $::meta::at(input, 0u32);
}

[[macro]] static $::meta::tokens last_tree(in $::meta::tokens input) {
    return $::meta::slice(input, 1u32, 1u32);
}

[[macro]] static $::meta::tokens joined(in $::meta::tokens input) {
    return $::meta::concat($::meta::slice(input, 0u32, 2u32),
                          $::meta::at(input, 2u32));
}

[[macro]] static $::meta::tokens empty_input(in $::meta::tokens input) {
    if ($::meta::len(input) == 0u32) return $::quote { 1u32 };
    return $::quote { 0u32 };
}

global u32 grouped_tree() { return first_tree! { (4u32 + 5u32) 12u32 }; }
global u32 sliced_tree() { return last_tree! { (4u32 + 5u32) 12u32 }; }
global u32 joined_trees() { return joined! { 7u32 + 8u32 }; }
global u32 empty_trees() { return empty_input! {}; }
