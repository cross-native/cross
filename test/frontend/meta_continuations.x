// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaContinuations {
    [[eval_only]] static u32 descend(in u32 depth, in u32 *cell) {
        *cell += 1u32;
        if (!depth) return *cell;
        u32 deepest = descend(depth - 1u32, cell);
        *cell += 2u32;
        return deepest;
    }
    [[eval_only]] static u32 assertions(in u32 depth) {
        if (depth) $::static_assert(assertions(depth - 1u32) == depth, "recursive assertion");
        return depth + 1u32;
    }
    [[eval_only]] static u32 append(in u32 digit, in u32 *cell) {
        *cell = *cell * 10u32 + digit;
        return digit;
    }
    [[eval_only]] static u32 arguments(in u32 first, in u32 second) {
        return first * 10u32 + second;
    }
    static $::meta::tokens check() {
        $::meta::buffer storage = $::meta::alloc(sizeof(u32));
        u32 *cell = (u32 *)$::meta::data(storage);
        *cell = 0u32;
        $::static_assert(descend(192u32, cell) == 193u32, "deepest live frame");
        $::static_assert(*cell == 577u32, "shared pointer during unwind");
        $::static_assert(assertions(192u32) == 193u32, "deep assertion continuations");
        *cell = 0u32;
        $::static_assert(arguments(append(1u32, cell), append(2u32, cell)) == 12u32,
                         "left-to-right arguments");
        $::static_assert(*cell == 12u32, "argument effects");
        $::static_assert((0u32 && append(9u32, cell)) == 0u32, "short circuit and");
        $::static_assert((1u32 || append(9u32, cell)) != 0u32, "short circuit or");
        $::static_assert((1u32 ? append(3u32, cell) : append(9u32, cell)) == 3u32,
                         "selected conditional");
        $::static_assert(*cell == 123u32, "only selected effects");
        return $::quote { 101u32 };
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check(); }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return check(); }
    syntax Check : expression { prefix "continuation_check"; match "(" ")"; expand expand; }
    syntax Check;
    static u32 run() {
        return apply!() == 101u32 && continuation_check() == 101u32 ? 101u32 : 0u32;
    }
}
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return MetaContinuations::run() == 101u32 ? 61u32 : 0u32;
}
