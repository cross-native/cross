// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct inner { u32 value; u8 text[4]; };
struct sample { u8 tag; struct inner items[2]; i32 signed_bits:5; u32 bits:7; };
union bits { u32 word; f32 real; };

[[eval_only]] static struct sample make_sample() {
    u32 sequence = 0u32;
    struct sample result = { .items[1].value = ++sequence,
        .items[0] = { .text = "abc", .value = ++sequence },
        .signed_bits = -3i32, .bits = 129u32, .tag = 7u8 };
    u32 table[4] = { [2] = 9u32, 10u32 };
    result.items[1].value += table[2] + table[3];
    return result;
}

[[eval_only]] static union bits make_union() {
    union bits value = { .real = 1.0f32 };
    return value;
}

[[eval_only]] static uptr initialized_objects_ok() {
    struct sample value = make_sample();
    struct sample selected = 1u32 ? value : make_sample();
    union bits word = make_union();
    return selected.items[1].value == 20u32 && value.tag == 7u8 && value.items[0].value == 2u32 &&
        value.items[0].text[3] == 0u8 && value.items[1].value == 20u32 &&
        value.signed_bits == -3i32 && value.bits == 1u32 && word.word == 0x3f800000u32;
}

global uptr initialized_objects_checked = initialized_objects_ok();
[[eval_only]] static uptr sample_parameter_ok(in struct sample value) {
    return value.items[1].value == 20u32 && value.signed_bits == -3i32;
}
global uptr conditional_object_checked = sample_parameter_ok(1u32 ? make_sample() : make_sample());

global struct sample evaluated_sample = make_sample();
global union bits evaluated_union = make_union();
struct outer { struct sample value; u32 last; };
global struct outer evaluated_outer = { .value = make_sample(), .last = 42u32 };

struct links { u32 *value; const u32 *other; u32 *slots[2]; };
struct node { struct node *next; u32 value; };

[[eval_only]] static struct links copied_links(in struct links value) {
    *value.value += 1u32;
    value.value = value.slots[0];
    return value;
}

[[eval_only]] static uptr overwritten_pointer_ok() {
    u32 number = 3u32;
    u32 *pointer = &number;
    u8 *representation = (u8 *)&pointer;
    for (uptr byte = 0uptr; byte < sizeof(pointer); ++byte)
        representation[byte] = 0u8;
    return !pointer;
}

[[eval_only]] static uptr pointer_objects_ok() {
    u32 number = 3u32;
    u32 *alias = &number;
    *alias = 7u32;
    u32 before = number++;
    uptr count = 2uptr;
    u32 varying[++count] = { [2] = 6u32 };
    uptr varying_size = sizeof(varying);
    count = 1uptr;
    u32 table[3] = { 10u32, 20u32, 30u32 };
    struct links original = { .value = alias, .other = alias, .slots = { &table[0], 0 } };
    struct links copy = original;
    copy.value = &table[1];
    u32 **member = &original.value;
    *member = &table[2];
    *original.value += 11u32;
    $::meta::buffer backing = $::meta::alloc(sizeof(struct links));
    struct links *stored = (struct links *)$::meta::data(backing);
    *stored = original;
    struct links loaded = *stored;
    struct links returned = copied_links(loaded);
    u32 **slots = loaded.slots;
    slots[1] = slots[0] + 1u32;
    ++slots[1];
    struct node self = { .next = &self, .value = 9u32 };
    struct node *same = &self;
    struct node replacement = { .next = same, .value = 12u32 };
    self = replacement;
    if (!alias) return 0uptr;
    if (alias && !original.slots[1]) number += 0u32;
    return varying_size == 12uptr && varying[2] == 6u32 && before == 7u32 && number == 8u32 && *copy.value == 20u32 &&
        *loaded.value == 42u32 && *loaded.other == 8u32 && *slots[1] == 42u32 &&
        *returned.value == 10u32 && returned.value != loaded.value && overwritten_pointer_ok() &&
        slots[1] == &table[2] && same->value == 12u32 && self.next == same &&
        original.slots[1] == (u32 *)0 && (bool)loaded.value && !original.slots[1];
}

global uptr pointer_objects_checked = pointer_objects_ok();
global u32 relocation_target = 17u32;
struct relocation_value { u8 tag; u32 *address; };
[[eval_only]] static struct relocation_value make_relocation() {
    struct relocation_value value = { .tag = 5u8, .address = &relocation_target };
    return value;
}
global struct relocation_value evaluated_relocation = make_relocation();
struct packed_value [[packed]] { u8 tag; u32 *address; i32 bits:5; uptr word; };
[[eval_only]] static struct packed_value make_packed() {
    struct packed_value value = { .tag = 3u8, .address = &relocation_target,
        .bits = -7i32, .word = 0x12345678uptr };
    value.bits += 2i32;
    return value;
}
global struct packed_value evaluated_packed = make_packed();

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 meta_object_entry() {
    struct sample local = $::eval(make_sample());
    union bits word = $::eval(make_union());
    struct relocation_value pointer = $::eval(make_relocation());
    return initialized_objects_checked == 1uptr && conditional_object_checked == 1uptr &&
        pointer_objects_checked == 1uptr &&
        *evaluated_packed.address == 17u32 && evaluated_packed.bits == -5i32 &&
        evaluated_packed.word == 0x12345678uptr && *evaluated_relocation.address == 17u32 &&
        pointer.address == &relocation_target && pointer.tag == 5u8 &&
        evaluated_sample.tag == 7u8 && local.items[0].value == 2u32 &&
        local.items[0].text[2] == 99u8 && local.items[0].text[3] == 0u8 &&
        local.items[1].value == 20u32 && local.items[1].text[0] == 0u8 &&
        local.signed_bits == -3i32 && local.bits == 1u32 &&
        evaluated_outer.value.items[1].value == 20u32 && evaluated_outer.last == 42u32 &&
        evaluated_union.word == 0x3f800000u32 && word.word == 0x3f800000u32 ? 53u32 : 0u32;
}
