// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens raw_scope(in $::meta::tokens input) {
    return $::quote {
        register u64 value "r10" = 3u64;
        $::unquote(input)
    };
}
[[naked, clobber("r10")]]
static u64 hygienic_raw(in u64 value "r9") -> "r8" {
    register u64 result "r8";
    raw_scope! { result = value; }
    $::_ret();
}

[[macro]] static $::meta::tokens raw_inline_scope(in $::meta::tokens input) {
    return $::quote {
        u64 value = 3u64;
        value += 2u64;
        $::unquote(input)
    };
}
[[raw_inline]] static u64 hygienic_raw_helper(in u64 value) {
    raw_inline_scope! { return value; }
}
[[naked, clobber("r10", "r11", "flags")]]
static u64 hygienic_raw_inline(in u64 value "r9") -> "r8" {
    register u64 result "r8";
    result = hygienic_raw_helper(value);
    $::_ret();
}
