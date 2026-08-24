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

global i32 aggregate_entry() {
    struct point point;
    struct point *pointer = &point;
    struct nested nested;
    union bits bits;
    struct packed_pair packed;
    struct aligned_value aligned;
    struct packed_array packed_array;
    struct node node;

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
           layout::namespace_record_value() + packed_array.values[1];
}
