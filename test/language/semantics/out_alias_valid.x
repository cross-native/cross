// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] static void local_alias(out i32 x) {
    i32 *p = &x;
    i32 *copy = p;
    *copy = 7;
    *p += 2;
}

[[noinline]] static void joined_alias(out i32 x, in bool choose) {
    i32 *p;
    if (choose) p = &x;
    else p = &x;
    *p = 13;
}

[[noinline]] static void selected_alias(out i32 x, in bool choose) {
    i32 *p = choose ? &x : &x;
    *p = 17;
}

[[noinline]] static void indirect_alias(out i32 x) {
    i32 other = 0;
    i32 *p = &other;
    i32 **pp = &p;
    *pp = &x;
    **pp = 23;
}

[[noinline]] static void saved_alias(out i32 x) {
    i32 other = 0;
    i32 *p = &x;
    i32 *saved = p;
    p = &other;
    *saved = 29;
    *p = 999;
}

[[noinline]] static void write_value(out i32 x) { x = 31; }
[[noinline]] static void call_alias(out i32 x) {
    i32 *p = &x;
    write_value(*p);
}

[[noinline]] static void loop_alias(out i32 x, in u32 count) {
    i32 *p = &x;
    *p = 0;
    for (u32 i = 0u32; i < count; ++i) *p += 1;
}

[[noinline]] static void goto_alias(out i32 x, in bool choose) {
    i32 *p;
    if (choose) goto second;
    p = &x;
    goto done;
second:
    p = &x;
done:
    *p = 37;
}

struct alias_record { u8 tag; uptr count; i32 values[2]; };
[[noinline]] static void member_alias(out struct alias_record x) {
    struct alias_record *p = &x;
    p->tag = 3u8;
    uptr *count = &p->count;
    *count = 41uptr;
    i32 *values = &p->values[0];
    *(values + 1) = 47;
    *(values + 1 - 1) = 43;
}

struct alias_bits { u32 first : 3; u32 second : 5; };
[[noinline]] static void bitfield_alias(out struct alias_bits x) {
    struct alias_bits *p = &x;
    p->first = 5u32;
    p->second = 19u32;
}

#ifdef CUSTOM_ALIAS_ABI
[[abi("odd_abi"), noinline]] static i32 register_alias(out i32 x) {
    i32 *p = &x;
    *p = 53;
    return 59;
}
[[abi("stack_result_abi"), noinline]] static i32 stack_alias(out i32 x) {
    i32 *p = &x;
    *p = 61;
    return 67;
}
struct alias_result { u64 first; u64 second; };
[[abi("memory_result_abi"), noinline]] static struct alias_result memory_alias(out i32 x) {
    i32 *p = &x;
    *p = 71;
    struct alias_result result = { 73u64, 79u64 };
    return result;
}
[[noinline]] static i32 manual_alias(out i32 x "r10d") -> "stack+32" {
    i32 *p = &x;
    *p = 83;
    return 89;
}
#endif

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
[[link_name("out_alias_entry")]] global i32 out_alias_entry() {
    i32 x = 0;
    local_alias(x); if (x != 9) return 1;
    joined_alias(x, 0); if (x != 13) return 2;
    joined_alias(x, 1); if (x != 13) return 3;
    selected_alias(x, 0); if (x != 17) return 4;
    selected_alias(x, 1); if (x != 17) return 5;
    indirect_alias(x); if (x != 23) return 6;
    saved_alias(x); if (x != 29) return 7;
    call_alias(x); if (x != 31) return 8;
    loop_alias(x, 7u32); if (x != 7) return 9;
    goto_alias(x, 0); if (x != 37) return 10;
    goto_alias(x, 1); if (x != 37) return 11;
    struct alias_record record;
    member_alias(record);
    if (record.tag != 3u8 || record.count != 41uptr ||
        record.values[0] != 43 || record.values[1] != 47) return 12;
    struct alias_bits bits;
    bitfield_alias(bits);
    if (bits.first != 5u32 || bits.second != 19u32) return 13;
#ifdef CUSTOM_ALIAS_ABI
    i32 result = register_alias(x); if (x != 53 || result != 59) return 14;
    result = stack_alias(x); if (x != 61 || result != 67) return 15;
    struct alias_result memory = memory_alias(x);
    if (x != 71 || memory.first != 73u64 || memory.second != 79u64) return 16;
    result = manual_alias(x); if (x != 83 || result != 89) return 17;
#endif
    return 0;
}
