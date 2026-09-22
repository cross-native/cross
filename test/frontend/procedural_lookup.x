// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace origin_tools {
    [[macro]] static $::meta::tokens leaf(in $::meta::tokens input) {
        return $::quote { 11u64 };
    }
    [[macro]] static $::meta::tokens root_leaf(in $::meta::tokens input) {
        return $::quote { 23u64 };
    }
    [[macro]] static $::meta::tokens definition(in $::meta::tokens input) {
        return $::quote { leaf!{} };
    }
    [[macro]] static $::meta::tokens parsed_definition(in $::meta::tokens input) {
        return $::meta::parse("leaf!{}");
    }
    [[macro]] static $::meta::tokens copied(in $::meta::tokens input) {
        return $::quote { $::unquote(input) };
    }
    [[macro]] static $::meta::tokens forwarded(in $::meta::tokens input) {
        return $::quote { copied! { $::unquote(input) } };
    }
    [[macro]] static $::meta::tokens relocate(in $::meta::tokens input) {
        return $::quote { namespace produced { global u64 result = $::unquote(input); } };
    }
}

namespace origin_imports {
    using origin_tools;
    [[macro]] static $::meta::tokens imported(in $::meta::tokens input) {
        return $::quote { leaf!{} };
    }
}

[[macro]] static $::meta::tokens root_leaf(in $::meta::tokens input) {
    return $::quote { 7u64 };
}
[[macro]] static $::meta::tokens root_definition(in $::meta::tokens input) {
    return $::quote { root_leaf!{} };
}

namespace origin_imports {
    // Imports from the earlier namespace block are no longer active here.
    [[macro]] static $::meta::tokens closed_import(in $::meta::tokens input) {
        return $::quote { root_leaf!{} };
    }
}

namespace origin_caller {
    [[macro]] static $::meta::tokens leaf(in $::meta::tokens input) {
        return $::quote { 99u64 };
    }
    [[macro]] static $::meta::tokens copied(in $::meta::tokens input) {
        return $::quote { 88u64 };
    }
    [[macro]] static $::meta::tokens root_leaf(in $::meta::tokens input) {
        return $::quote { 77u64 };
    }
    namespace produced {
        [[macro]] static $::meta::tokens leaf(in $::meta::tokens input) {
            return $::quote { 33u64 };
        }
    }
    origin_tools::relocate! { leaf!{} }
    [[noinline]] static i32 check() {
        if (origin_tools::definition!{} != 11u64) return 1;
        if (origin_tools::parsed_definition!{} != 11u64) return 2;
        if (origin_tools::copied!{leaf!{}} != 99u64) return 3;
        if (origin_tools::forwarded!{leaf!{}} != 99u64) return 4;
        if (origin_imports::imported!{} != 11u64) return 5;
        if (root_definition!{} != 7u64) return 6;
        if (origin_caller::produced::result != 99u64) return 7;
        if (origin_imports::closed_import!{} != 7u64) return 8;
        return 0;
    }
}
