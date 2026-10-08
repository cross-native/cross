// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

enum alignment_log { alignment_shift = 6u32 };

struct expression_aligned_record [[aligned(1u32 << alignment_shift)]] {
    u8 value;
};

struct expression_aligned_member {
    u8 first;
    u8 second [[aligned(1u32 << 5u32)]];
};

$::static_assert($::alignof(struct expression_aligned_record) == 64uptr,
                "record alignment expression");
$::static_assert(sizeof(struct expression_aligned_member) == 64uptr,
                "member alignment expression");

[[aligned(1uptr << (alignment_shift - 1u32)),
  aligned(sizeof(struct expression_aligned_record))]]
global u8 expression_aligned_object = 7u8;

[[generic(uptr N), aligned(N), noinline]]
static i32 generic_alignment() {
    [[aligned(N)]] stack u8 bytes[3];
    bytes[0] = 9u8;
    return ((uptr)&bytes[0] & (N - 1uptr)) == 0uptr &&
                   bytes[0] == 9u8
               ? (i32)N
               : 0;
}

static uptr type_alignment<T>() { return sizeof(T) * 8uptr; }

[[noinline]] static i32 lexical_alignment(in u32 input) {
    u64 seed;
    [[aligned(sizeof(seed) * 8uptr), aligned(sizeof(input))]] stack u8 outer[3];
    [[aligned(type_alignment<u64>())]] stack u8 generic[3];
    outer[0] = (u8)input;
    generic[0] = outer[0] + 1u8;
    if (((uptr)&outer[0] & 63uptr) != 0uptr ||
        ((uptr)&generic[0] & 63uptr) != 0uptr) return 0;
    {
        u16 seed;
        [[aligned(sizeof(seed) * 16uptr)]] stack u8 inner[3];
        inner[0] = generic[0];
        if (((uptr)&inner[0] & 31uptr) != 0uptr || inner[0] != (u8)(input + 1u32)) return 0;
    }
    return outer[0] == (u8)input && generic[0] == (u8)(input + 1u32);
}

[[link_name("alignment_expression_entry")]]
global i32 alignment_expression_entry() {
    [[aligned($::alignof(struct expression_aligned_record))]]
    stack u8 local[7];
    local[0] = 3u8;
    if (((uptr)&expression_aligned_object & 63uptr) != 0uptr ||
        ((uptr)&local[0] & 63uptr) != 0uptr) return 0;
    return expression_aligned_object == 7u8 && local[0] == 3u8 &&
                   generic_alignment::<64uptr>() == 64i32 &&
                   $::runtime(lexical_alignment(11u32)) == 1i32
               ? 1
               : 0;
}
