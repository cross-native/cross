// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
namespace ConstantHelpers {
static void fill(out u32 value) { value = 7u32; }
static u8 zero(in u32 seed) {
    u32 local;
    fill(local);
    return (u8)(local + seed - seed - 7u32);
}
static T identity<T>(in T value) { return value; }
static u32 index(in u32 value) { u32 data[2] = {value, 1u32}; return data[0u32] + data[1u32]; }
static u32 text_zero(in const u8 *value) { return value[0u32] == 'a' ? 0u32 : 1u32; }
typedef u32 Lanes [[ext_vector_type(4)]];
struct Box { Lanes value; };
typedef void (*Callback)();
struct AddressBox { u8 prefix; Lanes value; };
static struct AddressBox address_source;
static u32 *address_lane = &address_source.value[1u32];
static u32 address_array[4];
static void target() {}
static u8 address_zero(in const u32 *value) { return 0u8; }
static u8 callback_zero(in Callback value) { return 0u8; }
[[noinline]] static u32 *null_result() { return zero(13u32); }
[[syntax_expander]] static $::meta::tokens retain(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "definition")) };
}
syntax Retain : item { prefix "retained"; match definition:function_def; expand retain; }
syntax Retain;
retained [[noinline]] static u32 check(in u32 seed) {
    u32 *pointer = zero(3u32);
    pointer = identity<u8>(zero(5u32));
    Callback callback = zero(7u32);
    callback = (Callback)zero(9u32);
    if (pointer != zero(11u32) || identity<u8>(0u8) != callback || null_result() != zero(17u32)) return 1u32;
    if ((seed ? pointer : zero(19u32)) != zero(23u32)) return 2u32;
    u32 *text = text_zero("abc");
    if (text != zero(29u32)) return 3u32;
    struct Box box = {};
    box.value[0u32] = 31u32;
    box.value[1u32] = 37u32;
    box.value[2u32] = 41u32;
    box.value[3u32] = 43u32;
    if (box.value[index(identity<u32>(2u32))] != 43u32) return 4u32;
    if (sizeof(box.value[identity<u32>(3u32)]) != sizeof(u32)) return 5u32;
    if (pointer != address_zero(&address_source.value[1u32]) ||
        pointer != address_zero(&address_array[2u32]) ||
        callback != callback_zero(&target)) return 7u32;
    if (pointer != identity<u32>("abc"[1u32] - 'b')) return 8u32;
    if (address_lane != &address_source.value[1u32]) return 9u32;
    if (pointer != (identity<u8>(*"abc") - 'a') ||
        pointer != (identity<i8>(*((const i8 *)"\xff")) + 1i32)) return 10u32;
    return 61u32;
}
#ifdef CUSTOM_SYNTAX_ABI
[[noinline, abi("stack_result_abi")]] static u8 stack_zero() { return 0u8; }
[[noinline, abi("memory_result_abi")]] static u32 memory_index() { return 3u32; }
retained static u32 custom() {
    u32 *pointer = stack_zero();
    struct Box box = {};
    box.value[3u32] = 47u32;
    return pointer == stack_zero() && box.value[memory_index()] == 47u32 ? 61u32 : 0u32;
}
#endif
}
static volatile u32 seed = 1u32;
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifdef CUSTOM_SYNTAX_ABI
    if (ConstantHelpers::custom() != 61u32) return 6u32;
#endif
    return ConstantHelpers::check(seed);
}
