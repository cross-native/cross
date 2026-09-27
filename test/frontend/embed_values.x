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
struct eval_outer { u8 tag; struct eval_pair inner; struct eval_pair cells[2]; };
struct eval_packed_outer [[packed]] { u8 tag; struct eval_pair inner; };
union eval_union { u32 word; f32 real; u8 bytes[4]; };
struct eval_union_wrapper { u8 tag; union eval_union payload; };
struct eval_bits { u16 a : 3; u16 b : 5; u16 c : 8; };
struct eval_signed_bits { i16 value : 3; };

[[eval_only]] static uptr meta_modifying_operators_ok() {
    $::meta::buffer output = $::meta::alloc(6u32);
    u16 *words = (u16 *)$::meta::data(output);
    words[0u32] = 3u16; words[1u32] = 8u16; words[2u32] = 11u16;
    uptr index = 0uptr;
    words[index++] += 4u16;
    words[0u32] *= 2u16;
    u16 old = words[0u32]++;
    u16 now = ++words[0u32];
    words[0u32] >>= 1u32;
    u16 *cursor = words;
    u16 *before = cursor++;
    ++cursor;
    *cursor -= 3u16;
    --cursor;
    (*cursor)++;
    cursor -= 1u32;
    $::meta::buffer fields = $::meta::alloc(sizeof(struct eval_bits));
    struct eval_bits *bits = (struct eval_bits *)$::meta::data(fields);
    bits->a = 5u16;
    u16 old_field = bits->a++;
    bits->a += 5u16;
    --bits->a;
    $::meta::buffer floats = $::meta::alloc(4u32);
    f32 *real = (f32 *)$::meta::data(floats);
    *real = 1.5f32;
    *real += 2.0f32;
    f32 old_real = (*real)++;
    return index == 1uptr && old == 14u16 && now == 16u16 &&
           before == words && cursor == words && words[0u32] == 8u16 &&
           words[1u32] == 9u16 && words[2u32] == 8u16 &&
           old_field == 5u16 && bits->a == 2u16 &&
           old_real == 3.5f32 && *real == 4.5f32;
}

[[eval_only]] static struct eval_pair changed_pair(in struct eval_pair input) {
    input.tag = 9u8;
    return input;
}

[[eval_only]] static uptr record_value_copy_ok() {
    $::meta::buffer source = $::meta::alloc(sizeof(struct eval_pair));
    struct eval_pair *first = (struct eval_pair *)$::meta::data(source);
    first->tag = 7u8;
    first->value = 29uptr;
    struct eval_pair local = *first;
    first->value = 41uptr;
    struct eval_pair changed = changed_pair(local);
    $::meta::buffer destination = $::meta::alloc(sizeof(struct eval_pair));
    struct eval_pair *second = (struct eval_pair *)$::meta::data(destination);
    *second = changed;
    return first->value == 41uptr && local.value == 29uptr &&
           local.tag == 7u8 && changed.tag == 9u8 &&
           second->tag == 9u8 && second->value == 29uptr;
}

[[eval_only]] static uptr packed_record_value_ok() {
    $::meta::buffer source = $::meta::alloc(sizeof(struct eval_packed));
    struct eval_packed *first = (struct eval_packed *)$::meta::data(source);
    first->tag = 7u8;
    first->value = 29uptr;
    struct eval_packed value = *first;
    $::meta::buffer destination = $::meta::alloc(sizeof(struct eval_packed));
    struct eval_packed *second = (struct eval_packed *)$::meta::data(destination);
    *second = value;
    return second->tag == 7u8 && second->value == 29uptr;
}

[[eval_only]] static uptr union_record_value_ok() {
    $::meta::buffer source = $::meta::alloc(sizeof(union eval_union));
    union eval_union *first = (union eval_union *)$::meta::data(source);
    first->word = 0x3fc00000u32;
    union eval_union value = *first;
    $::meta::buffer destination = $::meta::alloc(sizeof(union eval_union));
    union eval_union *second = (union eval_union *)$::meta::data(destination);
    *second = value;
    return value.real == 1.5f32 && second->real == 1.5f32;
}

[[eval_only]] static uptr bit_field_record_value_ok() {
    struct eval_bits value;
    value.a = 5u16;
    value.b = 17u16;
    value.c = 0xabu16;
    $::meta::buffer destination = $::meta::alloc(sizeof(struct eval_bits));
    struct eval_bits *second = (struct eval_bits *)$::meta::data(destination);
    *second = value;
    return second->a == 5u16 && second->b == 17u16 && second->c == 0xabu16;
}

static $::meta::bytes zero_outer_bytes() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_outer));
    u8 *data = $::meta::data(output);
    for (uptr index = 0uptr; index < sizeof(struct eval_outer); ++index)
        data[index] = 0u8;
    return $::meta::freeze(output, sizeof(struct eval_outer));
}

[[eval_only]] static uptr nested_record_value_ok() {
    const struct eval_outer *initial =
        (const struct eval_outer *)$::meta::data(zero_outer_bytes());
    struct eval_outer value = *initial;
    value.inner.value = 23uptr;
    value.cells[1u32] = changed_pair(value.inner);
    $::meta::buffer destination = $::meta::alloc(sizeof(struct eval_outer));
    struct eval_outer *second = (struct eval_outer *)$::meta::data(destination);
    *second = value;
    struct eval_pair inner = second->cells[1u32];
    return initial->inner.value == 0uptr && inner.tag == 9u8 &&
           inner.value == 23uptr && second->inner.value == 23uptr;
}

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

static $::meta::bytes packed_record_bytes() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_packed));
    struct eval_packed *record =
        (struct eval_packed *)$::meta::data(output);
    record->tag = 0xa5u8;
    record->value = 0x12345678uptr;
    return $::meta::freeze(output, sizeof(struct eval_packed));
}

[[eval_only]] static uptr packed_record_member_ok() {
    const struct eval_packed *record =
        (const struct eval_packed *)$::meta::data(packed_record_bytes());
    return record->tag == 0xa5u8 && record->value == 0x12345678uptr;
}

[[eval_only]] static uptr nested_packed_record_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_packed_outer));
    struct eval_packed_outer *outer =
        (struct eval_packed_outer *)$::meta::data(output);
    outer->tag = 3u8;
    outer->inner.tag = 5u8;
    outer->inner.value = 7uptr;
    return outer->tag == 3u8 && outer->inner.tag == 5u8 &&
           outer->inner.value == 7uptr;
}

static $::meta::bytes union_reinterpreted_bytes() {
    $::meta::buffer output = $::meta::alloc(sizeof(union eval_union));
    union eval_union *value = (union eval_union *)$::meta::data(output);
    value->word = 0x3fc00000u32;
    return $::meta::freeze(output, sizeof(union eval_union));
}

[[eval_only]] static uptr union_member_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(union eval_union));
    union eval_union *value = (union eval_union *)$::meta::data(output);
    value->word = 0x3fc00000u32;
    bool first = value->real == 1.5f32;
    value->real = 2.0f32;
    return first && value->word == 0x40000000u32;
}

[[eval_only]] static uptr nested_union_member_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_union_wrapper));
    struct eval_union_wrapper *value =
        (struct eval_union_wrapper *)$::meta::data(output);
    value->tag = 7u8;
    value->payload.word = 0x3fc00000u32;
    return value->tag == 7u8 && value->payload.real == 1.5f32;
}

static $::meta::bytes bit_field_bytes() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_bits));
    struct eval_bits *value = (struct eval_bits *)$::meta::data(output);
    value->a = 5u16;
    value->b = 17u16;
    value->c = 0xabu16;
    return $::meta::freeze(output, sizeof(struct eval_bits));
}

[[eval_only]] static uptr bit_field_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_bits));
    struct eval_bits *value = (struct eval_bits *)$::meta::data(output);
    bool truncated = (value->a = 13u16) == 5u16;
    value->b = 17u16;
    value->c = 0xabu16;
    $::meta::buffer signed_output = $::meta::alloc(sizeof(struct eval_signed_bits));
    struct eval_signed_bits *signed_value =
        (struct eval_signed_bits *)$::meta::data(signed_output);
    signed_value->value = -1i16;
    return truncated && value->a == 5u16 && value->b == 17u16 &&
           value->c == 0xabu16 && signed_value->value == -1i16;
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

[[eval_only]] static uptr nested_record_member_ok() {
    $::meta::buffer output = $::meta::alloc(sizeof(struct eval_outer));
    struct eval_outer *outer = (struct eval_outer *)$::meta::data(output);
    outer->tag = 3u8;
    outer->inner.tag = 5u8;
    outer->inner.value = 7uptr;
    outer->cells[0u32].tag = 11u8;
    outer->cells[1u32].value = 13uptr;
    return (*outer).tag == 3u8 &&
           outer[0u32].inner.tag == 5u8 &&
           (*outer).inner.value == 7uptr &&
           outer->cells[0u32].tag == 11u8 &&
           outer->cells[1u32].value == 13uptr;
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

[[syntax_expander]] static $::meta::tokens parsed_asset(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax embed = body;
    while (!$::meta::is_production(embed, "embed_expression")) {
        if ($::meta::child_count(embed) != 1uptr) return $::quote { invalid_embed_tree };
        embed = $::meta::child(embed, 0uptr);
    }
    if ($::meta::child_count(embed) != 4uptr) return $::quote { invalid_embed_tree };
    for (uptr at = 0uptr; at < 4uptr; ++at)
        if (!$::meta::is_kind($::meta::child(embed, at), "token"))
            return $::quote { invalid_embed_tree };
    return $::meta::tokens(body);
}
syntax Asset : expression { prefix "asset"; match body:expr; expand parsed_asset; }
syntax Asset;

global const u8 original[] = $::embed("payload.bin");
global const u8 copied[] = copy_asset! { $::embed("payload.bin") };
global const u8 parsed[] = asset $::embed("payload.bin");
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
global const u8 packed_record[] = packed_record_bytes();
global uptr packed_record_member_checked = packed_record_member_ok();
global uptr nested_packed_record_checked = nested_packed_record_ok();
global const u8 union_bytes[] = union_reinterpreted_bytes();
global uptr union_member_checked = union_member_ok();
global uptr nested_union_member_checked = nested_union_member_ok();
global uptr record_value_copy_checked = record_value_copy_ok();
global uptr packed_record_value_checked = packed_record_value_ok();
global uptr union_record_value_checked = union_record_value_ok();
global uptr bit_field_record_value_checked = bit_field_record_value_ok();
global uptr nested_record_value_checked = nested_record_value_ok();
global uptr meta_modifying_operators_checked = meta_modifying_operators_ok();
global const u8 bit_fields[] = bit_field_bytes();
global uptr bit_field_checked = bit_field_ok();
global uptr record_array_member_checked = record_array_member_ok();
global uptr nested_record_member_checked = nested_record_member_ok();
global uptr direct_nested_record_checked =
    ((const struct eval_outer *)$::meta::data(zero_outer_bytes()))->inner.tag == 0u8;
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
