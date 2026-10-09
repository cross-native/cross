// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Translation-time layout of typedefs that request alignment. Every target
// must agree on these facts; the script also checks the emitted objects.

typedef u32 wide_u32 [[aligned(16)]];
typedef wide_u32 wider_u32 [[aligned(32)]];
typedef wider_u32 kept_u32 [[aligned(8)]];
typedef u64 natural_u64 [[aligned(4)]];
typedef u8 line_u8 [[aligned(64)]];
typedef u8 *padded_pointer [[aligned(16)]];
typedef u32 row4[4] [[aligned(32)]];
struct pair { u32 x; u32 y; };
typedef struct pair pair16 [[aligned(16)]];
typedef pair16 pair64 [[aligned(64)]];

// Requests are required constants like every other `aligned` placement.
enum shift { five = 5 };
struct triple { u64 a; u64 b; u64 c; };
typedef u16 by_enum [[aligned(1u32 << five)]];
typedef u16 by_sizeof [[aligned(sizeof(struct triple) + 8uptr)]];
typedef u16 by_alignof [[aligned($::alignof(wide_u32) * 4uptr)]];

// Effective alignment is the largest request or the natural alignment, and
// the size rounds up to it.
$::static_assert(sizeof(wide_u32) == 16uptr && $::alignof(wide_u32) == 16uptr, "scalar");
$::static_assert(sizeof(wider_u32) == 32uptr && $::alignof(wider_u32) == 32uptr, "chain");
$::static_assert(sizeof(kept_u32) == 32uptr && $::alignof(kept_u32) == 32uptr, "largest request");
$::static_assert(sizeof(natural_u64) == 8uptr && $::alignof(natural_u64) == 8uptr, "natural");
$::static_assert(sizeof(line_u8) == 64uptr && $::alignof(line_u8) == 64uptr, "byte");
$::static_assert(sizeof(padded_pointer) == 16uptr && $::alignof(padded_pointer) == 16uptr, "pointer");
$::static_assert(sizeof(row4) == 32uptr && $::alignof(row4) == 32uptr, "array type");
$::static_assert(sizeof(pair16) == 16uptr && $::alignof(pair16) == 16uptr, "record type");
$::static_assert(sizeof(pair64) == 64uptr && $::alignof(pair64) == 64uptr, "record chain");
$::static_assert(sizeof(by_enum) == 32uptr && $::alignof(by_enum) == 32uptr, "enumerator");
$::static_assert(sizeof(by_sizeof) == 32uptr && $::alignof(by_sizeof) == 32uptr, "sizeof");
$::static_assert(sizeof(by_alignof) == 64uptr && $::alignof(by_alignof) == 64uptr, "alignof");

// Arrays stride by the rounded size; members are placed at the alignment.
$::static_assert(sizeof(wide_u32[3]) == 48uptr && $::alignof(wide_u32[3]) == 16uptr, "array");
$::static_assert(sizeof(row4[2]) == 64uptr && sizeof(line_u8[2][3]) == 384uptr, "nested arrays");
struct leading { u8 first; wide_u32 value; };
struct trailing { wide_u32 value; u8 last; };
struct three { u8 first; wide_u32 value; u8 last; };
union overlay { u8 byte; wide_u32 value; };
struct nested { u8 tag; struct three inner; };
struct packed_member [[packed]] { u8 tag; wide_u32 value; };
struct packed_plain [[packed]] { u8 tag; u32 value; };
struct packed_array [[packed]] { u8 tag; wide_u32 values[2]; };
$::static_assert(sizeof(struct leading) == 32uptr && $::alignof(struct leading) == 16uptr, "leading");
$::static_assert(sizeof(struct trailing) == 32uptr, "trailing");
$::static_assert(sizeof(struct three) == 48uptr, "three");
$::static_assert(sizeof(union overlay) == 16uptr && $::alignof(union overlay) == 16uptr, "union");
$::static_assert(sizeof(struct nested) == 64uptr && $::alignof(struct nested) == 16uptr, "nested");
$::static_assert(sizeof(struct packed_member) == 32uptr, "packing keeps the request");
$::static_assert(sizeof(struct packed_plain) == 5uptr, "packing still packs");
$::static_assert(sizeof(struct packed_array) == 48uptr, "packing keeps element requests");

// Generic instances substitute and raise the requested alignment.
[[generic(uptr N)]]
static uptr generic_value() {
    typedef u16 local [[aligned(N)]];
    local values[2];
    return sizeof(values) + $::alignof(local);
}
static uptr generic_type<T>() {
    typedef T padded [[aligned(64)]];
    return sizeof(padded) + $::alignof(T);
}
$::static_assert(generic_value::<8uptr>() == 24uptr, "generic value");
$::static_assert(generic_type<u32>() == 68uptr, "generic type");
global uptr generic_aligned = generic_type<wide_u32>();

// Translation-time evaluation observes the same offsets and strides.
static uptr find_byte(in u8 *bytes, in uptr size, in u8 marker) {
    for (uptr index = 0uptr; index < size; ++index)
        if (bytes[index] == marker) return index;
    return 999uptr;
}
static uptr evaluated_layout() {
    struct three record;
    u8 *raw = (u8 *)(void *)&record;
    for (uptr index = 0uptr; index < sizeof(record); ++index) raw[index] = 0u8;
    record.value = 0x02020202u32;
    record.last = 3u8;
    wide_u32 values[3];
    u8 *cells = (u8 *)(void *)&values[0];
    for (uptr index = 0uptr; index < sizeof(values); ++index) cells[index] = 0u8;
    *(&values[0] + 2) = 0x06060606u32;
    return find_byte(raw, sizeof(record), 2u8) + find_byte(raw, sizeof(record), 3u8) * 100uptr +
           find_byte(cells, sizeof(values), 6u8) * 10000uptr;
}
$::static_assert(evaluated_layout() == 323216uptr, "evaluated layout");

// Values convert like the base type, and pointer compatibility is unchanged.
global u32 convert(in wide_u32 value) {
    u32 plain = value;
    wide_u32 back = plain + 1u32;
    return back;
}
global u32 (*through)(in u32 value) = convert;
global u32 plain_object = 4u32;
global wide_u32 *wide_pointer = &plain_object;
global u32 *join(in bool which, in wide_u32 *wide, in u32 *plain) {
    u32 *narrow = wide;
    wide_u32 *widened = plain;
    return which ? widened : narrow;
}
global struct pair unwrap(in pair16 value) {
    struct pair plain = value;
    pair64 rewrapped = plain;
    return rewrapped;
}

// Function types that differ only in a parameter's or result's request are
// the same callable type: they convert both ways, redeclare each other, and
// deduce the base type.
typedef wide_u32 (*wide_result)(in wide_u32 value);
typedef u32 (*plain_result)(in u32 value);
global wide_u32 widen(in u32 value) { return value + 1u32; }
global plain_result plain_from_wide = widen;
global wide_result wide_from_plain = convert;
wide_u32 redeclared(in wide_u32 value);
global u32 redeclared(in u32 value) { return value; }
global u32 call_both(in plain_result first, in wide_result second) {
    plain_result narrowed = second;
    wide_result widened = first;
    return narrowed(1u32) + widened(2u32) + redeclared(3u32);
}
static uptr result_size<T>(in T (*callee)(in u32 value)) { return sizeof(T); }
global uptr deduced_result = result_size(widen);

// Emitted objects keep their alignment, size, and value bytes.
global wide_u32 initialized = 7u32;
global line_u8 zeroed;
global wide_u32 initialized_table[2] = {1u32, 2u32};
