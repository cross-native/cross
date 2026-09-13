struct point {
    i32 x;
    i32 y;
};

struct nested {
    u8 tag;
    struct point points[2];
};

union bits {
    u64 integer;
    f64 floating;
};

struct packed_pair [[packed]] {
    u8 tag;
    u32 value;
};

struct aligned_value [[aligned(16)]] {
    u8 byte;
};

struct packed_array [[packed]] {
    u8 tag;
    u32 values[2];
};

struct node {
    i32 value;
    struct node *next;
};

struct text_value {
    u8 text[4];
    i32 value;
};

struct shadowed {
    i32 outer;
};

namespace layout {
    struct shadowed {
        u8 prefix;
        i32 inner;
    };

    static i32 namespace_record_value() {
        struct shadowed value;
        value.inner = 2;
        return value.inner;
    }
}

global struct point global_point;
global struct nested global_nested;
global union bits global_bits;
global struct point initialized_point = { .y = 13, .x = 12 };
global u32 initialized_array[] = { 1u32, 2u32, [4] = 5u32 };
global struct nested initialized_nested = {
    .tag = 3u8,
    .points = { { .x = 4, .y = 5 }, { .x = 6, .y = 7 } },
};
global union bits initialized_bits = {
    .integer = 0x3ff0000000000000u64,
};
global struct node initialized_node = {
    .value = 9,
    .next = &initialized_node,
};
global struct text_value initialized_text = {
    .text = "AB",
    .value = 4,
};
global u8 initialized_narrow[] = { 300u32 };
global uptr initialized_layout[] = {
    sizeof(struct point), $::alignof(struct node),
};
global i32 initializer_calls = 0;

[[runtime_only, noinline]]
static i32 next_initializer_value() {
    initializer_calls += 1;
    return initializer_calls;
}

global i32 aggregate_entry() {
    struct point point;
    struct point *pointer = &point;
    struct nested nested;
    union bits bits;
    struct packed_pair packed;
    struct aligned_value aligned;
    struct packed_array packed_array;
    struct node node;
    struct point initialized_local = { 1, 2 };
    const struct point initialized_const = { 7, 8 };
    stack volatile struct point initialized_volatile = { 9, 10 };
    u32 initialized_local_array[] = { 3u32, [3] = 4u32 };
    u32 reset_array[4] = { [1] = 2u32, 3u32 };
    u32 optional_equal[2] = { [1] 6u32 };
    struct nested initialized_local_nested = {
        .tag = 5u8,
        .points[1].y = 6,
    };
    union bits initialized_local_bits = {
        .integer = 0x3ff0000000000000u64,
    };
    struct node initialized_local_node = {
        .value = 14,
        .next = &initialized_local_node,
    };
    struct text_value initialized_local_text = {
        .text = "C",
        .value = 5,
    };
    u8 initialized_local_narrow[] = { 300u32 };
    static struct point initialized_static = { 11, 12 };
    static uptr initialized_static_layout[] = { sizeof(struct point) };
    struct point ordered = {
        next_initializer_value(), next_initializer_value(),
    };

    point.x = 3;
    point.y = 4;
    pointer->x += 2;
    point.x++;

    nested.tag = 2u8;
    nested.points[0].x = 6;
    nested.points[1].y = 7;

    packed.tag = 8u8;
    packed.value = 9u32;
    packed_array.values[1] = 5u32;

    global_point.x = 10;
    global_point.y = 11;
    bits.integer = 0x3ff0000000000000u64;
    global_bits.integer = bits.integer;
    node.value = 3;
    node.next = &node;

    return point.x + pointer->y + nested.tag +
           nested.points[0].x + nested.points[1].y +
           packed.tag + packed.value + global_point.x +
           global_point.y + (global_bits.floating == 1.0f64) +
           (global_nested.points[1].x == 0) +
           ($::alignof(packed) == 1uptr) +
           ($::alignof(aligned) == 16uptr) + node.next->value +
           layout::namespace_record_value() + packed_array.values[1] +
           initialized_point.x + initialized_point.y +
           initialized_array[0] + initialized_array[1] +
           initialized_array[2] + initialized_array[3] +
           initialized_array[4] +
           initialized_nested.tag + initialized_nested.points[0].x +
           initialized_nested.points[0].y +
           initialized_nested.points[1].x +
           initialized_nested.points[1].y +
           (initialized_bits.floating == 1.0f64) +
           initialized_node.value +
           (initialized_node.next == &initialized_node) +
           initialized_text.text[0] + initialized_text.text[1] +
           initialized_text.text[2] + initialized_text.value +
           initialized_narrow[0] +
           initialized_local.x + initialized_local.y +
           initialized_const.x + initialized_const.y +
           initialized_volatile.x + initialized_volatile.y +
           initialized_local_array[0] + initialized_local_array[1] +
           initialized_local_array[2] + initialized_local_array[3] +
           initialized_local_nested.tag +
           initialized_local_nested.points[0].x +
           initialized_local_nested.points[1].y +
           (initialized_local_bits.floating == 1.0f64) +
           initialized_local_node.value +
           (initialized_local_node.next == &initialized_local_node) +
           initialized_local_text.text[0] +
           initialized_local_text.text[1] +
           initialized_local_text.value + initialized_local_narrow[0] +
           initialized_static.x + initialized_static.y +
           initialized_static_layout[0] + optional_equal[1] +
           ordered.x + ordered.y + (initializer_calls == 2) +
           (sizeof(initialized_array) == 20uptr) +
           (sizeof(initialized_local_array) == 16uptr) +
           initialized_layout[0] + initialized_layout[1] +
           reset_array[0] + reset_array[1] + reset_array[2] +
           reset_array[3];
}
