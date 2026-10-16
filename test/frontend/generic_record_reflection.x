// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Instances of generic records are ordinary types to reflection and to
// embedding: macros inspect their members and layout, equal arguments give
// equal reflected types, and an embedded instance is designated by its tag.

struct list<T> { T *items; uptr count; };
struct buf<T, uptr N> { T data[N]; u8 used; };
union either<T> { T value; u32 bits; };
struct tagged { u32 kind; struct list<u32>; };
struct deeper { u8 flags; struct tagged; };

// Decimal digits as one integer token.
static $::meta::tokens number(in uptr value) {
    $::meta::buffer text = $::meta::alloc(20);
    u8 *digits = $::meta::data(text);
    u8 reversed[20];
    uptr count = 0;
    do {
        reversed[count] = (u8)(48 + value % 10);
        value = value / 10;
        ++count;
    } while (value != 0);
    for (uptr i = 0; i < count; ++i) digits[i] = reversed[count - 1 - i];
    return $::meta::parse($::meta::freeze(text, count));
}

// Generates the reflected member count, size, and offset of the last member
// of an instance, and a function that returns a zero value of it.
[[macro]]
static $::meta::tokens shape($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    uptr n = $::meta::member_count(t);
    return $::quote {
        static uptr shape_count() { return $::unquote(number(n)); }
        static uptr shape_size() { return $::unquote(number($::meta::size(t))); }
        static uptr shape_last() { return $::unquote(number($::meta::member_offset(t, n - 1))); }
        static $::unquote($::meta::type_tokens(t)) shape_zero() {
            $::unquote($::meta::type_tokens(t)) value = {};
            return value;
        }
    };
}

shape! { struct buf<u16, 3> }

[[macro]]
static $::meta::tokens compare($::meta::tokens input) {
    $::meta::type a = $::meta::type($::quote { struct buf<u16, 2 + 1> });
    $::meta::type b = $::meta::type($::quote { struct buf<u16, 3uptr> });
    $::meta::type c = $::meta::type($::quote { struct buf<u16, 4> });
    $::meta::type l = $::meta::type($::quote { struct list<u32> });
    $::meta::type m = $::meta::member_type($::meta::type($::quote { struct tagged }), 1);
    $::static_assert($::meta::same_type(a, b), "equal arguments");
    $::static_assert(!$::meta::same_type(a, c), "different arguments");
    $::static_assert($::meta::same_type(l, m), "embedded instance");
    $::static_assert($::meta::is_type_kind(l, "struct"), "record kind");
    $::static_assert($::meta::is_type_kind($::meta::type($::quote { union either<u8> }), "union"), "union kind");
    $::static_assert($::meta::is_type_kind($::meta::member_type(l, 0), "pointer"), "pointer member");
    $::static_assert($::meta::same_type($::meta::element($::meta::member_type(l, 0)),
                                        $::meta::type($::quote { u32 })), "substituted member type");
    $::static_assert($::meta::count($::meta::member_type(a, 0)) == 3, "value argument extent");
    $::static_assert($::meta::member_count($::meta::type($::quote { struct tagged })) == 2, "embedded member");
    return $::quote { static u32 compare_ok() { return 1; } };
}

compare! {}

// Members that a macro generates from its input see the record's parameters.
[[macro]]
static $::meta::tokens twice($::meta::tokens input) {
    return $::quote { $::unquote(input) first; $::unquote(input) second; };
}
struct twin<T> { twice!{ T } };
$::static_assert(sizeof(struct twin<u16>) == 4 && sizeof(struct twin<u32>) == 8, "generated members");

$::static_assert(shape_count() == 2, "reflected member count");
$::static_assert(shape_size() == sizeof(struct buf<u16, 3>), "reflected size");
$::static_assert(shape_last() == $::offsetof(struct buf<u16, 3>, used), "reflected offset");
$::static_assert($::offsetof(struct tagged, count) == $::offsetof(struct tagged, list.count), "promoted member");

static u32 check_embedding() {
    u32 words[2] = { 5, 6 };
    struct tagged t = { 9, { words, 2 } };
    if (t.count != 2 || t.items[1] != 6 || t.list.count != 2) return 2;
    struct list<u32> *view = &t;
    if (view->items[0] != 5) return 3;
    struct tagged *back = (struct tagged *)view;
    if (back->kind != 9) return 4;
    struct deeper d = {};
    d.items = words;
    d.count = 1;
    if (d.tagged.list.items[0] != 5 || d.list.count != 1) return 5;
    struct list<u32> *inner = &d;
    if (inner->count != 1) return 6;
    struct buf<u16, 3> z = shape_zero();
    if (z.used != 0 || z.data[2] != 0) return 7;
    struct twin<u8> pair = { 1, 2 };
    if (pair.first + pair.second != 3) return 8;
    return 1;
}

$::static_assert(check_embedding() == 1, "embedded instances at translation time");

global u32 generic_record_reflection_entry() {
    if ($::runtime(check_embedding()) != 1) return 2;
    if (compare_ok() != 1) return 3;
    return 1;
}
