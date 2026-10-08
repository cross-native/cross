// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace EvaluatedOutputs {
[[syntax_expander]] static $::meta::tokens retain(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "definition");
    return $::quote { $::unquote($::meta::parse("function_def", $::quote {
        $::unquote(node)
    }, $::syntax::context(input))) };
}
syntax Retain : item { prefix "retained"; match definition:function_def; expand retain; }
syntax Retain;

retained [[noinline]] static u32 aliases(out u32 first, out u32 second, in u32 *actual) {
    first = 5u32;
    second = 9u32;
    return *actual;
}
retained [[noinline]] static void independent(inout u32 first, inout u32 second) {
    first += 1u32;
    second += 2u32;
}
retained [[noinline]] static void fill(out u32 value) { value = 300u32; }
retained [[noinline]] static void capture(out u32 value, inout u32 *cursor, in u32 *next) {
    value = 41u32;
    cursor = next;
}
retained [[noinline]] static void indexed(out u32 value, in u32 *index) {
    *index = 2u32;
    value = 43u32;
}
retained [[noinline]] static u32 pointer_cell(inout u32 *value, in u32 *next) {
    *value = 31u32;
    value = next;
    *value = 37u32;
    return *value;
}
retained [[noinline]] static void effect(in u32 *value) { *value += 1u32; }
retained [[noinline]] static T generic<T>(inout T value, in T delta) {
    value += delta;
    return value;
}
struct Padded { u8 tag; uptr number; u32 *pointer; };
struct Bits { u32 low : 3; u32 high : 5; u8 tag; };
union Choice { struct Padded large; u8 small; };
typedef u32 Lanes [[ext_vector_type(4)]];
struct VectorBox { Lanes value; };
retained [[noinline]] static void record(out struct Padded value, in u32 *pointer) {
    value.tag = 7u8;
    value.number = 54uptr;
    value.pointer = pointer;
}
retained [[noinline]] static void bits(out struct Bits value) {
    value.low = 5u32;
    value.high = 17u32;
    value.tag = 11u8;
}
retained [[noinline]] static void union_small(out union Choice value) { value.small = 13u8; }
retained [[noinline]] static void lanes(out struct VectorBox value) {
    value.value[0u32] = 19u32;
    value.value[1u32] = 19u32;
    value.value[2u32] = 19u32;
    value.value[3u32] = 19u32;
}
retained [[noinline]] static void floating(inout f64 value) { value += 1.5f64; }

#ifdef CUSTOM_SYNTAX_ABI
retained [[noinline, abi("stack_result_abi")]] static u32 stack_result(out u32 value) {
    value = 23u32;
    return 29u32;
}
retained [[noinline, abi("memory_result_abi")]] static u32 memory_result(inout u32 value) {
    value += 31u32;
    return 37u32;
}
#endif

static u32 run(in u32 seed) {
    u32 scalar;
    fill(scalar);
    if (scalar != 300u32) return 1u32;
    scalar = seed;
    u32 saved = aliases(scalar, scalar, &scalar);
    if (saved != seed) return 32u32;
    if (scalar != 9u32) return 33u32;
    independent(scalar, scalar);
    if (scalar != 11u32) return 3u32;
    u8 narrow;
    fill(narrow);
    if (narrow != 44u8) return 4u32;
    u32 first = 1u32, second = 2u32;
    u32 *cursor = &first;
    capture(*cursor, cursor, &second);
    if (first != 41u32 || cursor != &second || second != 2u32) return 5u32;
    cursor = &first;
    if (pointer_cell(cursor, &second) != 37u32 || first != 31u32 ||
        second != 37u32 || cursor != &second) return 6u32;
    u32 values[3] = {};
    u32 index = 0u32;
    indexed(values[index++], &index);
    if (index != 2u32) return 27u32;
    if (values[0u32] != 43u32) return 28u32;
    if (values[2u32] != 0u32) return 29u32;
    u32 effects = 0u32;
    fill(effect(&effects));
    fill(effects++);
    independent(1u32, 2u32);
    const u32 discarded = 47u32;
    fill(discarded);
    if (effects != 2u32 || discarded != 47u32) return 8u32;
    if (generic(scalar, 3u32) != 14u32 || scalar != 14u32) return 9u32;
    struct Padded aggregate;
    record(aggregate, &first);
    if (aggregate.tag != 7u8 || aggregate.number != 54uptr ||
        aggregate.pointer != &first || *aggregate.pointer != 31u32) return 10u32;
    struct Padded records[2];
    record(records[1u32], &second);
    if (records[1u32].number != 54uptr || *records[1u32].pointer != 37u32) return 11u32;
    struct Bits packed_bits;
    bits(packed_bits);
    fill(packed_bits.low);
    if (packed_bits.low != 4u32 || packed_bits.high != 17u32 || packed_bits.tag != 11u8) return 12u32;
    union Choice choice;
    union_small(choice);
    if (choice.small != 13u8) return 13u32;
    struct VectorBox vector_box;
    lanes(vector_box);
    fill(vector_box.value[2u32]);
    if (vector_box.value[0u32] != 19u32 || vector_box.value[2u32] != 300u32 ||
        vector_box.value[3u32] != 19u32) return 14u32;
    f64 fraction = 2.5f64;
    floating(fraction);
    if (fraction != 4.0f64) return 15u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (stack_result(scalar) != 29u32 || scalar != 23u32) return 16u32;
    if (memory_result(scalar) != 37u32 || scalar != 54u32) return 17u32;
#endif
    return 61u32;
}
$::static_assert($::eval(run(3u32)) == 61u32, "evaluated parameter cells and captured copy-out");
static $::meta::tokens checked(in $::meta::tokens input) {
    if (run(7u32) != 61u32) return $::quote { 0u32 };
    return input;
}
[[macro]] static $::meta::tokens check(in $::meta::tokens input) { return checked(input); }
$::static_assert(check!(61u32) == 61u32, "ordinary output calls from a meta helper");
}
static volatile u32 eval_output_seed = 3u32;
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() { return EvaluatedOutputs::run(eval_output_seed); }
