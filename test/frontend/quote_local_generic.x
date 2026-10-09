// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A quoted name bound to a block-scope typedef or tag of a generic helper
// denotes that type of the instance that runs.
static $::meta::tokens value_tokens<T>(in T value) {
    typedef T Value;
    return $::quote { sizeof(Value) };
}

static $::meta::tokens holder_tokens<T>(in T value) {
    struct Holder { T item; u8 tag; };
    return $::quote { struct Holder };
}

// A typedef of the instance's record.
static $::meta::tokens held_tokens<T>(in T value) {
    struct Holder { T item; u8 tag; };
    typedef struct Holder Held;
    return $::quote { Held };
}

static $::meta::tokens buffer_tokens<uptr N>() {
    typedef u8 Buffer[N];
    return $::quote { Buffer };
}

static $::meta::tokens mode_tokens<T>(in T value) {
    enum Mode [[underlying(u8)]] { Off = 3, On = (u8)sizeof(T) };
    return $::quote { enum Mode };
}

[[macro]] static $::meta::tokens value16(in $::meta::tokens input) { return value_tokens(0u16); }
[[macro]] static $::meta::tokens value64(in $::meta::tokens input) { return value_tokens(0u64); }
[[macro]] static $::meta::tokens holder16(in $::meta::tokens input) { return holder_tokens(0u16); }
[[macro]] static $::meta::tokens holder64(in $::meta::tokens input) { return holder_tokens(0u64); }
[[macro]] static $::meta::tokens held32(in $::meta::tokens input) { return held_tokens(0u32); }
[[macro]] static $::meta::tokens buffer7(in $::meta::tokens input) { return buffer_tokens<7>(); }
[[macro]] static $::meta::tokens mode8(in $::meta::tokens input) { return mode_tokens(0u8); }

global u32 quote_local_generic_entry() {
    holder64!() wide;
    wide.item = 0x1122334455667788u64;
    wide.tag = 5u8;
    holder16!() narrow;
    narrow.item = 7u16;
    held32!() held;
    held.item = 9u32;
    buffer7!() buffer;
    buffer[6] = 4u8;
    mode8!() mode = (mode8!())8u8;
    return value16!() == 2uptr && value64!() == 8uptr &&
           sizeof(holder64!()) == 16uptr && sizeof(holder16!()) == 4uptr &&
           sizeof(held32!()) == 8uptr && sizeof(buffer7!()) == 7uptr && sizeof(mode8!()) == 1uptr &&
           wide.item == 0x1122334455667788u64 && wide.tag == 5u8 && narrow.item == 7u16 &&
           held.item == 9u32 && buffer[6] == 4u8 && (u8)mode == 8u8;
}
