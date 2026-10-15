// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Type reflection: macros generate code from a record's members, its
// bit-field layout in the target's byte order, and an enumeration.

struct sample {
    u8 head;
    u16 low : 3;
    u16 : 2;
    u16 mid : 6;
    u16 : 0;
    u16 high : 7;
    i32 value;
    struct { u8 first; u8 second; };
    u32 tail;
};

enum color [[underlying(u16)]] { red, green = 5, blue };
enum { quad_lanes = 4 };
typedef u32 quad[quad_lanes];
typedef u32 u32x4 [[vector_size(16)]];

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

// A string literal token spelling an identifier.
static $::meta::tokens quoted(in $::meta::tokens name) {
    $::meta::bytes spelling = $::meta::spelling(name);
    uptr length = $::meta::len(spelling);
    $::meta::buffer text = $::meta::alloc(length + 2);
    u8 *target = $::meta::data(text);
    const u8 *source = $::meta::data(spelling);
    target[0] = 34;
    for (uptr i = 0; i < length; ++i) target[i + 1] = source[i];
    target[length + 1] = 34;
    return $::meta::token("string", $::meta::freeze(text, length + 2));
}

// Reads `size` bytes at `offset` as an unsigned integer in the target's order.
static u64 read_unit(in const u8 *bytes, in uptr offset, in uptr size) {
    u64 unit = 0;
    for (uptr i = 0; i < size; ++i) {
#if $::target::byte_order == $::target::order_big
        unit = (unit << 8) | bytes[offset + i];
#else
        unit = unit | ((u64)bytes[offset + i] << (8 * i));
#endif
    }
    return unit;
}

// Generates `check_<record>`, which compares each named member read directly
// with the same member extracted from the object's bytes through its
// reflected offset, bit position, and width; a member-wise sum; and a table
// of member names.
[[macro]]
static $::meta::tokens reflect_members($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    $::meta::tokens checks = $::meta::parse("");
    $::meta::tokens sum = $::meta::parse("0u64");
    $::meta::tokens names = $::meta::parse("");
    for (uptr i = 0; i < $::meta::member_count(t); ++i) {
        $::meta::tokens name = $::meta::member_name(t, i);
        $::meta::type m = $::meta::member_type(t, i);
        if ($::meta::len(name) == 0 || !$::meta::is_type_kind(m, "integer")) continue;
        uptr width = $::meta::member_bit_width(t, i);
        if (width == 0) width = $::meta::bits(m);
        $::meta::tokens mask = $::meta::parse("0xffffffffffffffffu64");
        if (width < 64)
            mask = $::quote { ((1u64 << $::unquote(number(width))) - 1u64) };
        checks = $::meta::concat(checks, $::quote {
            if ((((read_unit(bytes, $::unquote(number($::meta::member_offset(t, i))),
                             $::unquote(number($::meta::size(m))))
                   >> $::unquote(number($::meta::member_bit_offset(t, i)))) & $::unquote(mask))
                 != ((u64)p->$::unquote(name) & $::unquote(mask)))) return 0;
        });
        sum = $::quote { $::unquote(sum) + (u64)p->$::unquote(name) };
        names = $::meta::concat(names, $::quote { $::unquote(quoted(name)), });
    }
    return $::quote {
        static u32 check_layout(const $::unquote($::meta::type_tokens(t)) *p) {
            const u8 *bytes = (const u8 *)(const void *)p;
            $::unquote(checks)
            return 1;
        }
        static u64 member_sum(const $::unquote($::meta::type_tokens(t)) *p) {
            return $::unquote(sum);
        }
        static const u8 *member_names[] = { $::unquote(names) };
    };
}

reflect_members! { struct sample }

// A switch over the reflected enumerators.
[[macro]]
static $::meta::tokens reflect_enumerators($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    $::static_assert($::meta::is_type_kind(t, "enum") && $::meta::count(t) == 0, "enumeration");
    $::static_assert($::meta::bits($::meta::element(t)) == 16, "underlying type");
    $::meta::tokens cases = $::meta::parse("");
    for (uptr i = 0; i < $::meta::enumerator_count(t); ++i)
        cases = $::meta::concat(cases, $::quote {
            case $::unquote($::meta::enumerator_name(t, i)): return $::unquote(number(i));
        });
    return $::quote {
        static u32 ordinal($::unquote($::meta::type_tokens(t)) value) {
            switch (value) { $::unquote(cases) }
            return 99;
        }
    };
}

reflect_enumerators! { enum color }

// The tokens of a reflected type keep denoting it where a declaration of the
// same name hides it.
[[macro]]
static $::meta::tokens shadowed_sum($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    return $::quote {
        struct sample { u8 other; };
        $::unquote($::meta::type_tokens(t)) local = {};
        local.value = 40;
        local.tail = 2;
        return local.value + (i32)local.tail;
    };
}

static i32 hidden_name() { shadowed_sum! { struct sample } }

// A block-scope record is complete where an expansion after it runs.
[[macro]]
static $::meta::tokens sum_fields($::meta::tokens input) {
    $::meta::type t = $::meta::type($::meta::slice(input, 0, 2));
    $::meta::tokens object = $::meta::at(input, 2);
    $::meta::tokens sum = $::meta::parse("0");
    for (uptr i = 0; i < $::meta::member_count(t); ++i)
        sum = $::quote { $::unquote(sum) + $::unquote(object).$::unquote($::meta::member_name(t, i)) };
    return sum;
}

static i32 local_sum() {
    struct point { i32 x; i32 y; i32 z; };
    struct point p = { 3, 4, 5 };
    return sum_fields! { struct point p };
}

// Facts about kinds, sizes, and the other type constructors.
[[macro]]
static $::meta::tokens facts($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    $::static_assert($::meta::is_type_kind(t, "struct") && !$::meta::is_type_kind(t, "union"), "kind");
    $::static_assert($::meta::member_count(t) == 9, "members include unnamed ones");
    $::static_assert($::meta::len($::meta::member_name(t, 2)) == 0, "unnamed bit-field");
    $::static_assert($::meta::member_bit_width(t, 2) == 2, "unnamed bit-field width");
    $::static_assert($::meta::member_bit_width(t, 4) == 0, "zero-width bit-field");
    $::static_assert($::meta::len($::meta::member_name(t, 7)) == 0, "anonymous member");
    $::static_assert($::meta::is_type_kind($::meta::member_type(t, 7), "struct"), "embedded");
    $::static_assert($::meta::size(t) == sizeof(struct sample), "size");
    $::static_assert($::meta::align(t) == $::alignof(struct sample), "alignment");
    $::static_assert($::meta::member_offset(t, 8) == $::offsetof(struct sample, tail), "offset");
    $::static_assert($::meta::is_signed($::meta::member_type(t, 6)), "signed");
    $::static_assert($::meta::bits($::meta::member_type(t, 6)) == 32, "width");
    $::meta::type array = $::meta::type($::quote { const u16[3] });
    $::static_assert($::meta::is_type_kind(array, "array") && $::meta::count(array) == 3, "array");
    $::static_assert($::meta::same_type($::meta::element(array), $::meta::type($::quote { const u16 })), "element");
    $::meta::type pointer = $::meta::type($::quote { struct sample * });
    $::static_assert($::meta::count(pointer) == 1 && $::meta::same_type($::meta::element(pointer), t), "pointee");
    $::meta::type wide = $::meta::type($::quote { f64 });
    $::static_assert($::meta::is_type_kind(wide, "floating") && $::meta::bits(wide) == 64, "floating");
    $::static_assert($::meta::same_type($::meta::type($::quote { quad }),
                                        $::meta::type($::quote { u32[2 * 2] })), "array extents");
    $::meta::type vector = $::meta::type($::quote { u32x4 });
    $::static_assert($::meta::is_type_kind(vector, "vector") && $::meta::count(vector) == 4, "vector");
    $::meta::type flag = $::meta::type($::quote { bool });
    $::static_assert($::meta::is_type_kind(flag, "bool") && !$::meta::is_type_kind(flag, "integer"), "bool");
    $::static_assert($::meta::bits(flag) == 8 && !$::meta::is_signed(flag), "bool width");
    $::static_assert($::meta::same_type($::meta::type($::meta::type_tokens(t)), t), "round trip");
    return $::meta::parse("");
}

facts! { struct sample }

// Input tokens resolve at the invocation; quoted ones where they are written,
// including names declared in the macro's own blocks. A helper may take and
// return type values.
typedef u32 word_t;

static $::meta::type second_member(in $::meta::type t) { return $::meta::member_type(t, 1); }

[[macro]]
static $::meta::tokens resolution($::meta::tokens input) {
    typedef u8 word_t;
    struct only_here { u64 wide; u16 narrow; };
    $::meta::type from_input = $::meta::type(input);
    $::meta::type from_quote = $::meta::type($::quote { word_t });
    $::meta::type local = $::meta::type($::quote { struct only_here });
    $::static_assert($::meta::size(from_input) == 4, "input resolves at the invocation");
    $::static_assert($::meta::size(from_quote) == 1, "quote resolves where written");
    $::static_assert($::meta::size(second_member(local)) == 2, "macro-local record");
    return $::meta::parse("");
}

resolution! { word_t }

global u32 type_reflection_entry() {
    struct sample value = {};
    value.head = 0xa5;
    value.low = 5;
    value.mid = 33;
    value.high = 100;
    value.value = -7;
    value.first = 11;
    value.second = 12;
    value.tail = 0x01020304;
    if ($::runtime(check_layout(&value)) != 1) return 2;
    if ($::runtime(member_sum(&value)) != 0xa5u64 + 5 + 33 + 100 - 7 + 0x01020304) return 3;
    const u8 *expected[] = { "head", "low", "mid", "high", "value", "tail" };
    if (sizeof(member_names) != sizeof(expected)) return 4;
    for (uptr i = 0; i < 6; ++i)
        for (uptr k = 0; expected[i][k] != 0 || member_names[i][k] != 0; ++k)
            if (expected[i][k] != member_names[i][k]) return 5;
    if (ordinal(red) != 0 || ordinal(green) != 1 || $::runtime(ordinal(blue)) != 2) return 6;
    if ($::runtime(hidden_name()) != 42) return 7;
    if ($::runtime(local_sum()) != 12) return 8;
    return 1;
}

$::static_assert(hidden_name() == 42, "reflected type tokens are hygienic");
$::static_assert(ordinal(blue) == 2, "enumerator names");
$::static_assert(local_sum() == 12, "block-scope record");
