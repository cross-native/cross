// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct Pair { u32 value; };
static u32 effects;
static volatile u32 value;
static volatile u32 watched[2];
[[noinline, runtime_only]] static u32 next() { ++effects; return 7u32; }
static uptr width<T>(in T input) { return sizeof(T); }

[[noinline, runtime_only]] static u32 inspect(in u32 input, in u32 *pointer, in struct Pair *record) {
    $::assume(pointer != 0);
    $::assume(*pointer == 3u32);
    $::assume(pointer[0uptr] == 3u32);
    $::assume(record->value == 3u32);
    struct Pair object = {3u32};
    $::assume(object.value == 3u32);
    $::assume(&value != 0);
    $::assume(&watched[0uptr] != 0);
    $::assume(sizeof(++effects) == sizeof(u32));
    $::assume(sizeof(next()) == sizeof(u32));
    $::assume(sizeof(value) == sizeof(u32));
    $::assume($::alignof(value) == $::alignof(u32));
    $::assume($::expect(input, 1u32 + 2u32) == 7u32);
    return $::expect(next(), sizeof(uptr) + 1uptr);
}

[[macro]] static $::meta::tokens generated(in $::meta::tokens input) {
    return $::quote {
        $::assume(sizeof(++effects) == sizeof(u32));
        $::expect(7u32, 2u32 + 3u32);
    };
}

#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    u32 item = 3u32;
    struct Pair record = {3u32};
    generated!{}
    if (inspect(7u32, &item, &record) != 7u32 || effects != 1u32) return 1u32;
    if (width($::expect((u16)1, 1u32)) != sizeof(u16)) return 2u32;
    return 61u32;
}
