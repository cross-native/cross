// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

enum generic_shade [[underlying(u8)]] {
    shade_dark = 3u8,
    shade_light = 7u8,
};

[[noinline]]
global enum generic_shade shade_identity<enum generic_shade Shade>() {
    return Shade;
}

namespace palette {
    enum tone [[underlying(i16)]] { low = -17i16, high = 127i16 };
    typedef enum tone Tone;

    [[noinline]]
    static Tone alias_identity<Tone Value>() { return Value; }
}

using palette;

[[noinline]]
static enum palette::tone qualified_identity<enum palette::tone Value>() { return Value; }

[[noinline]]
static enum tone imported_identity<enum tone Value>() { return Value; }

// A builtin prefix in an identifier does not make it a value parameter.
[[noinline]]
static u64Type dependent_identity<u64Type, u64Type Value>() { return Value; }

global u64 generic_enum_value_entry() {
    enum generic_shade dark = shade_identity::<shade_dark>();
    enum generic_shade light = shade_identity::<shade_light>();
    enum generic_shade equivalent = shade_identity::<7u8>();
    return dark == shade_dark && light == shade_light &&
           equivalent == shade_light &&
           palette::alias_identity::<palette::low>() == -17 &&
           qualified_identity::<palette::high>() == 127 &&
           imported_identity::<palette::low>() == -17 &&
           dependent_identity::<enum palette::tone, -17>() == palette::low;
}
