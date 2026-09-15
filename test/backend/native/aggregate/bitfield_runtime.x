// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct flags {
    u32 ready : 1;
    u32 mode : 3;
    u32 : 0;
    i32 delta : 5;
    u32 tail : 4;
};

struct tiny_flags {
    bool first : 1;
    bool second : 1;
    u8 tag : 3;
};

struct packed_flags [[packed]] {
    u8 prefix;
    u32 first : 3;
    u32 second : 5;
    u8 suffix;
};

enum signed_mode [[underlying(i8)]] {
    negative_mode = -1,
    positive_mode = 2,
};

struct enum_flags {
    enum signed_mode mode : 3;
};

struct layout_seed {
    u8 prefix;
    u32 value;
};

struct layout_width_flags {
    u32 sized : sizeof(struct layout_seed);
    u32 aligned : $::alignof(struct layout_seed);
};

struct crossing_flags {
    u32 first : 30;
    u32 second : 3;
};

struct padding_flags {
    u8 first : 2;
    u8 : 3;
    u8 second : 3;
};

union union_flags {
    u32 low : 4;
    u32 all;
};

global struct flags initialized_flags = {
    .ready = 1u32,
    .mode = 5u32,
    .delta = -3,
    .tail = 9u32,
};

global struct padding_flags initialized_padding = { 3u8, 5u8 };

[[noinline]]
static struct flags echo_flags(in struct flags value) {
    return value;
}

global i32 bitfield_entry() {
    struct flags local = { 1u32, 5u32, -3, 9u32 };
    struct flags *pointer = &local;
    struct tiny_flags tiny = { 1, 0, 6u8 };
    struct packed_flags packed = { 3u8, 5u32, 17u32, 7u8 };
    struct enum_flags choice = { negative_mode };
    struct layout_width_flags layout_width = { 0xa5u32, 9u32 };
    struct crossing_flags crossing = { 0x1234567u32, 5u32 };
    struct padding_flags padding = { 3u8, 5u8 };
    stack volatile struct flags volatile_flags = {
        1u32, 5u32, -3, 9u32,
    };
    union union_flags overlay = { .all = 0x12345678u32 };
    struct flags echoed = echo_flags(local);

    const u32 old_tail = local.tail--;
    local.mode += 2u32;
    ++pointer->delta;
    tiny.second = 1;
    overlay.low = 5u32;
    volatile_flags.mode += 1u32;

    if (sizeof(struct flags) != 12) return 101;
    if ($::alignof(struct flags) != 4) return 102;
    if (sizeof(struct tiny_flags) != 2) return 103;
    if (sizeof(struct packed_flags) != 6) return 104;
    if (sizeof(struct layout_width_flags) != 4) return 105;
    if (sizeof(struct crossing_flags) != 8) return 106;
    if (sizeof(struct padding_flags) != 1) return 107;
    if (local.ready != 1u32) return 108;
    if (local.mode != 7u32) return 109;
    if (local.delta != -2) return 110;
    if (old_tail != 9u32) return 111;
    if (local.tail != 8u32) return 112;
    if (!tiny.first) return 113;
    if (!tiny.second) return 114;
    if (tiny.tag != 6u8) return 115;
    if (packed.prefix != 3u8) return 116;
    if (packed.first != 5u32) return 117;
    if (packed.second != 17u32) return 118;
    if (packed.suffix != 7u8) return 119;
    if (choice.mode != negative_mode) return 120;
    if (layout_width.sized != 0xa5u32) return 121;
    if (layout_width.aligned != 9u32) return 122;
    if (crossing.first != 0x1234567u32) return 123;
    if (crossing.second != 5u32) return 124;
    if (padding.first != 3u8) return 125;
    if (padding.second != 5u8) return 126;
    if (initialized_padding.first != 3u8) return 127;
    if (initialized_padding.second != 5u8) return 128;
    if (volatile_flags.mode != 6u32) return 129;
    if (initialized_flags.ready != 1u32) return 130;
    if (initialized_flags.mode != 5u32) return 131;
    if (initialized_flags.delta != -3) return 132;
    if (initialized_flags.tail != 9u32) return 133;
    if (echoed.mode != 5u32) return 134;
    if (overlay.low != 5u32) return 135;
    if (overlay.all != 0x52345678u32 &&
        overlay.all != 0x12345675u32) return 136;
    return 36;
}
