// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u32 u32x4 [[ext_vector_type(4)]];
struct vector_box { u8 tag; u32x4 value; };

[[eval_only]] static u32x4 changed_lane(in u32x4 value) {
    value[2u32] = 0xaabbccddu32;
    return value;
}

[[eval_only]] static $::meta::bytes copied_vector_bytes() {
    $::meta::buffer output = $::meta::alloc(32u32);
    u32 *words = (u32 *)$::meta::data(output);
    words[0u32] = 1u32;
    words[1u32] = 2u32;
    words[2u32] = 0x11223344u32;
    words[3u32] = 4u32;
    u32x4 *vectors = (u32x4 *)$::meta::data(output);
    u32x4 snapshot = changed_lane(vectors[0u32]);
    vectors[1u32] = snapshot;
    return $::meta::freeze(output, 32u32);
}

[[eval_only]] static uptr vector_record_ok() {
    $::meta::buffer source = $::meta::alloc(16u32);
    u32 *words = (u32 *)$::meta::data(source);
    words[0u32] = 1u32;
    words[1u32] = 2u32;
    words[2u32] = 3u32;
    words[3u32] = 4u32;
    $::meta::buffer destination = $::meta::alloc(sizeof(struct vector_box));
    struct vector_box *box = (struct vector_box *)$::meta::data(destination);
    box->value = changed_lane(((u32x4 *)$::meta::data(source))[0u32]);
    return box->value[0u32] == 1u32 &&
           box->value[2u32] == 0xaabbccddu32;
}

global const u8 vector_bytes[] = copied_vector_bytes();
global uptr vector_record_checked = vector_record_ok();

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 meta_vector_entry() {
    return vector_record_checked == 1uptr &&
           vector_bytes[0u32] == 1u8 &&
           vector_bytes[8u32] == 0x44u8 &&
           vector_bytes[9u32] == 0x33u8 &&
           vector_bytes[24u32] == 0xddu8 &&
           vector_bytes[25u32] == 0xccu8 &&
           vector_bytes[31u32] == 0u8 ? 47u32 : 0u32;
}
