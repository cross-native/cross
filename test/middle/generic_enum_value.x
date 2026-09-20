// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

enum generic_shade [[underlying(u8)]] {
    shade_dark = 3u8,
    shade_light = 7u8,
};

[[generic(enum generic_shade Shade), noinline]]
global enum generic_shade shade_identity() {
    return Shade;
}

namespace palette {
    enum tone [[underlying(i16)]] { low = -17i16, high = 127i16 };
    typedef enum tone Tone;

    [[generic(Tone Value), noinline]]
    static Tone alias_identity() { return Value; }
}

using palette;

[[generic(enum palette::tone Value), noinline]]
static enum palette::tone qualified_identity() { return Value; }

[[generic(enum tone Value), noinline]]
static enum tone imported_identity() { return Value; }

// A builtin prefix in an identifier does not make it a value parameter.
[[generic(u64Type, u64Type Value), noinline]]
static u64Type dependent_identity() { return Value; }

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
