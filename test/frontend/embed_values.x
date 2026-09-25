// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static $::meta::bytes rotate(in $::meta::bytes source) {
    uptr count = $::meta::len(source);
    return $::meta::concat($::meta::slice(source, 1u32, count - 1u32),
                          $::meta::slice(source, 0u32, 1u32));
}

static uptr invert_bytes(in const u8 *source, in uptr length,
                         in u8 *destination, in uptr capacity) {
    uptr cursor = 0u32;
    const u8 *one_past = source + length;
    while (cursor < length && cursor < capacity) {
        destination[cursor] = *(source + cursor) ^ 255u32;
        ++cursor;
    }
    return cursor;
}

static u8 read_one(in const u8 *source) { return source[0u32]; }
static i8 read_signed(in const i8 *source) { return source[0u32]; }
static u32 read_word(in const u32 *source) { return source[0u32]; }
static u32 read_opaque(in const void *source) {
    return ((const u32 *)source)[0u32];
}
static uptr same_opaque(in const void *left, in const void *right) {
    return left == right;
}
static f32 read_float(in const f32 *source) { return source[0u32]; }

static $::meta::bytes packed_float() {
    $::meta::buffer output = $::meta::alloc(4u32);
    f32 *pointer = (f32 *)$::meta::data(output);
    pointer[0u32] = 1.5f32;
    return $::meta::freeze(output, 4u32);
}

static $::meta::bytes packed_double() {
    $::meta::buffer output = $::meta::alloc(8u32);
    f64 *pointer = (f64 *)$::meta::data(output);
    pointer[0u32] = -0.0f64;
    return $::meta::freeze(output, 8u32);
}

static $::meta::bytes packed_quad() {
    $::meta::buffer output = $::meta::alloc(16u32);
    f128 *pointer = (f128 *)$::meta::data(output);
    pointer[0u32] = 1.5f128;
    return $::meta::freeze(output, 16u32);
}

static $::meta::bytes packed_extended() {
    $::meta::buffer output = $::meta::alloc(16u32);
    f80 *pointer = (f80 *)$::meta::data(output);
    pointer[0u32] = 1.5f80;
    return $::meta::freeze(output, 16u32);
}

static $::meta::bytes packed_pointer_scalars() {
    $::meta::buffer output = $::meta::alloc($::target::pointer_bytes * 2u32);
    uptr *integer = (uptr *)$::meta::data(output);
    integer[0u32] = 0x12345678u32;
    fptr *real = (fptr *)($::meta::data(output) + $::target::pointer_bytes);
    real[0u32] = 1.5fptr;
    return $::meta::freeze(output, $::target::pointer_bytes * 2u32);
}

[[eval_only]] static uptr read_extended_without_padding() {
    $::meta::buffer output = $::meta::alloc(16u32);
    u8 *bytes = $::meta::data(output);
#if $::target::byte_order == $::target::order_little
    uptr start = 0u32;
#else
    uptr start = 6u32;
#endif
    uptr index = 0u32;
    while (index < 10u32) {
        bytes[start + index] = 0u32;
        ++index;
    }
#if $::target::byte_order == $::target::order_little
    bytes[7u32] = 0xc0u32;
    bytes[8u32] = 0xffu32;
    bytes[9u32] = 0x3fu32;
#else
    bytes[6u32] = 0x3fu32;
    bytes[7u32] = 0xffu32;
    bytes[8u32] = 0xc0u32;
#endif
    const f80 *real = (const f80 *)bytes;
    return real[0u32] == 1.5f80;
}

[[eval_only]] static iptr pointer_distance() {
    $::meta::buffer value = $::meta::alloc(8u32);
    u32 *start = (u32 *)$::meta::data(value);
    u32 *end = start + 2u32;
    return end - start;
}

[[eval_only]] static uptr pointer_order() {
    $::meta::buffer value = $::meta::alloc(8u32);
    u32 *start = (u32 *)$::meta::data(value);
    u32 *end = start + 2u32;
    return start < end && end >= start && start != end &&
           start == start && end - start == 2;
}

static $::meta::bytes packed_words() {
    $::meta::buffer output = $::meta::alloc(8u32);
    u32 *pointer = (u32 *)$::meta::data(output);
    pointer[0u32] = 0x12345678u32;
    pointer[1u32] = 0x9abcdef0u32;
    u32 *end = pointer + 2u32;
    return $::meta::freeze(output, 8u32);
}

typedef u16 (*word_row)[2];
typedef u16 (*word_grid)[2][2];
typedef uptr (*pointer_row)[2];
typedef uptr pointer_pair[2];
struct eval_pair { u8 tag; uptr value; };
struct eval_packed [[packed]] { u8 tag; uptr value; };
struct eval_collection { u8 tag; u16 values[2]; };

[[eval_only]] static uptr target_record_layout_ok() {
    return sizeof(struct eval_pair) == 2u32 * $::target::pointer_bytes &&
           $::alignof(struct eval_pair) == $::target::pointer_bytes &&
           sizeof(struct eval_packed) == 1u32 + $::target::pointer_bytes &&
           $::alignof(struct eval_packed) == 1u32;
}

[[eval_only]] static uptr record_member_ok() {
    $::meta::buffer output = $::meta::alloc(2u32 * sizeof(struct eval_pair));
    struct eval_pair *first = (struct eval_pair *)$::meta::data(output);
    struct eval_pair *second = first + 1u32;
    first->tag = 7u8;
    first->value = 11uptr;
    second->tag = 13u8;
    second->value = 17uptr;
    return first->tag == 7u8 && first->value == 11uptr &&
           second->tag == 13u8 && second->value == 17uptr &&
           second - first == 1iptr;
}

[[eval_only]] static uptr record_array_member_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_collection));
    struct eval_collection *record =
        (struct eval_collection *)$::meta::data(output);
    record->tag = 3u8;
    record->values[0u32] = 0x1122u16;
    record->values[1u32] = 0x3344u16;
    return record->tag == 3u8 && record->values[0u32] == 0x1122u16 &&
           record->values[1u32] == 0x3344u16;
}

static $::meta::bytes packed_rows() {
    $::meta::buffer output = $::meta::alloc(8u32);
    word_row rows = (word_row)$::meta::data(output);
    rows[0u32][0u32] = 0x1122u16;
    rows[0u32][1u32] = 0x3344u16;
    rows[1u32][0u32] = 0x5566u16;
    rows[1u32][1u32] = 0x7788u16;
    return $::meta::freeze(output, 8u32);
}

[[eval_only]] static uptr array_view_ok() {
    $::meta::buffer output = $::meta::alloc(8u32);
    word_row rows = (word_row)$::meta::data(output);
    rows[0u32][0u32] = 0x1122u16;
    rows[0u32][1u32] = 0x3344u16;
    rows[1u32][0u32] = 0x5566u16;
    rows[1u32][1u32] = 0x7788u16;
    word_grid grid = (word_grid)$::meta::data(output);
    u16 *start = rows[1u32];
    u16 *end = start + 2u32;
    u16 *first_end = rows[0u32] + 2u32;
    return rows[0u32][1u32] == 0x3344u16 &&
           rows[1u32][0u32] == 0x5566u16 &&
           grid[0u32][1u32][1u32] == 0x7788u16 &&
           rows + 2u32 - rows == 2iptr &&
           end - start == 2iptr && start < end &&
           first_end == start;
}

[[eval_only]] static uptr target_sized_array_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(pointer_pair));
    pointer_row row = (pointer_row)$::meta::data(output);
    row[0u32][0u32] = 3uptr;
    row[0u32][1u32] = 5uptr;
    return row[0u32][1u32] == 5uptr &&
           sizeof(pointer_pair) == 2u32 * $::target::pointer_bytes &&
           $::alignof(pointer_pair) == $::target::pointer_bytes &&
           row + 1u32 - row == 1iptr;
}

static $::meta::bytes signed_output() {
    $::meta::buffer output = $::meta::alloc(1u32);
    i8 *pointer = (i8 *)$::meta::data(output);
    pointer[0u32] = -1i8;
    return $::meta::freeze(output, 1u32);
}

static $::meta::bytes inverted(in $::meta::bytes source) {
    $::meta::buffer output = $::meta::alloc($::meta::len(source));
    uptr length = invert_bytes($::meta::data(source), $::meta::len(source),
                               $::meta::data(output), $::meta::cap(output));
    return $::meta::freeze(output, length);
}

static uptr buffer_capacity(in $::meta::buffer value) {
    return $::meta::cap(value);
}

static $::meta::buffer make_empty_buffer() {
    return $::meta::alloc(0u32);
}

[[eval_only]] static uptr empty_buffer_capacity() {
    $::meta::buffer value = make_empty_buffer();
    $::meta::buffer alias = value;
    uptr capacity = buffer_capacity(alias);
    $::meta::bytes empty = $::meta::freeze(value, 0u32);
    return capacity + $::meta::len(empty);
}

[[macro]] static $::meta::tokens copy_asset(in $::meta::tokens input) {
    return $::quote { $::unquote(input) };
}

global const u8 original[] = $::embed("payload.bin");
global const u8 copied[] = copy_asset! { $::embed("payload.bin") };
global u8 rotated[5] = rotate($::embed("payload.bin"));
global u8 encoded[] = inverted($::embed("payload.bin"));
global u8 encoded_slice[3] = inverted($::meta::slice($::embed("payload.bin"), 1u32, 3u32));
global const u8 signed_encoded[] = signed_output();
global const u8 words[] = packed_words();
global const u8 rows[] = packed_rows();
global uptr array_view_checked = array_view_ok();
global uptr target_sized_array_checked = target_sized_array_ok();
global uptr target_record_layout_checked = target_record_layout_ok();
global uptr record_member_checked = record_member_ok();
global uptr record_array_member_checked = record_array_member_ok();
global const u8 float_bytes[] = packed_float();
global const u8 double_bytes[] = packed_double();
global const u8 quad_bytes[] = packed_quad();
global const u8 extended_bytes[] = packed_extended();
global const u8 pointer_scalars[] = packed_pointer_scalars();
global uptr extended_padding_ok = read_extended_without_padding();
global iptr meta_pointer_distance = pointer_distance();
global uptr meta_pointer_order_ok = pointer_order();
global u32 source_word = read_word((const u32 *)$::meta::data($::embed("payload.bin")));
global u32 opaque_word = read_opaque($::meta::data($::embed("payload.bin")));
global uptr opaque_equal = same_opaque(
    $::meta::data($::embed("payload.bin")),
    $::meta::data($::embed("payload.bin")));
global f32 float_value = read_float((const f32 *)$::meta::data(packed_float()));
global uptr asset_size = $::meta::len($::embed("payload.bin"));
global uptr static_size = sizeof(original);
global uptr empty_capacity = empty_buffer_capacity();
global u8 first_byte = $::meta::at($::embed("payload.bin"), 0u32);
global u8 zero_byte = $::meta::at($::embed("payload.bin"), 1u32);
global u8 high_byte = $::meta::at($::embed("payload.bin"), 3u32);
global u8 second_byte = read_one($::meta::data($::embed("payload.bin")) + 1u32);
global i8 signed_high = read_signed((const i8 *)$::meta::data(
    $::meta::slice($::embed("payload.bin"), 3u32, 1u32)));
global i32 signed_extended = read_signed((const i8 *)$::meta::data(
    $::meta::slice($::embed("payload.bin"), 3u32, 1u32)));

#if $::target::byte_order == $::target::order_little
global u32 endian_read_ok = read_word((const u32 *)$::meta::data(
    $::embed("payload.bin"))) == 0xff800041u32;
global u32 endian_write_ok = $::meta::at(packed_words(), 0u32) == 0x78u32;
#else
global u32 endian_read_ok = read_word((const u32 *)$::meta::data(
    $::embed("payload.bin"))) == 0x410080ffu32;
global u32 endian_write_ok = $::meta::at(packed_words(), 0u32) == 0x12u32;
#endif
