// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "meta_nested_switch.x"

namespace MetaSwitchValues {
static uptr one() { return 1uptr; }
[[noinline]] static u32 runtime(in u8 selector) {
    u16 type_only;
    switch (selector) {
    case sizeof(type_only) + 254uptr: return 3u32; // u8 promotes to i32.
    case one(): return 7u32;
    default: return 9u32;
    }
}
static $::meta::tokens helper(in $::meta::tokens input) {
    if (!MetaNestedSwitch::check()) return $::quote {0u32};
    u8 type_only;
    u32 selector_count = 0u32;
    switch (++selector_count) {
    case sizeof(++type_only): break; // Does not read/write type_only.
    default: return $::quote {0u32};
    }
    if (selector_count != 1u32) return $::quote {0u32};
    // A case value that depends on the invocation is computed when the switch
    // executes.
    switch (selector_count) { case selector_count: break; default: return $::quote {0u32}; }
    uptr count = $::meta::len(input);
    u8 dynamic[count];
    switch (count) { case sizeof(dynamic): break; default: return $::quote {0u32}; }
    switch (4uptr) {
        u32 scoped_type;
        case sizeof(scoped_type): break; // Storage is not yet established at selection.
        default: return $::quote {0u32};
    }
    switch ((i8)-1i32) {
    case -1i64:
        switch (1u32) { case one(): break; default: return $::quote {0u32}; }
        break;
    case 256u64: return $::quote {0u32};
    default: return $::quote {0u32};
    }
    enum Kind [[underlying(i16)]] { Negative = -1i16 } kind = Negative;
    switch (kind) {
    case -1i128: break;
    case 1u8: return $::quote {0u32};
    default: return $::quote {0u32};
    }
    switch ((uptr)-1iptr) {
    case (uptr)-1iptr: break;
    default: return $::quote {0u32};
    }
    switch (1u128 << 100u32) {
    case (1u128 << 100u32): break;
    case (1u128 << 100u32) + 1u128: return $::quote {0u32};
    default: return $::quote {0u32};
    }
    if (0u32) {
        switch (0u32) { if (0u32) { case 1u32: ; } while (0u32) { case 2u32: break; } }
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return helper($::quote { $::unquote($::syntax::node(input, "value")) });
}
syntax Checked : expression { prefix "checked_switch"; match "(" value:expr ")"; expand expand; }
syntax Checked;
static u32 run() { return apply!(17u32) + checked_switch (19u32) + runtime(1u8) + runtime(2u8); }
}

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return MetaNestedSwitch::check() && MetaSwitchValues::run() == 52u32 ? 61u32 : 0u32;
}
