// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*odd_callback)(in i32 value) [[abi("odd_abi")]];
typedef i32 (*stack_callback)(in i32 value) [[abi("stack_result_abi")]];
struct generic_memory_pair { u64 low; u64 high; };
typedef struct generic_memory_pair (*memory_callback)(in u32 value)
    [[abi("memory_result_abi")]];
typedef i32 (*default_callback)(in i32 value);
typedef i32 (*canonical_callback)(in i32 value) [[abi("test_sysv")]];
typedef i32 (*alias_callback)(in i32 value) [[abi("test_abi")]];

[[abi("test_abi"), noinline]] static i32 alias_add_four(in i32 value) {
    return value + 4i32;
}

static T copy<T>(in T value) {
    return value;
}

static T [[noinline]] copy_interleaved<T>(in T value) {
    return value;
}

static T copy_trailing<T>(in T value) [[noinline]] {
    return value;
}

static T invoke_stack<T>(
    in T (*callback)(in T value) [[abi("stack_result_abi")]],
    in T value) {
    return callback(value);
}

static T invoke_memory<T>(
    in T (*callback)(in u32 value) [[abi("memory_result_abi")]],
    in u32 value) {
    return callback(value);
}

[[abi("odd_abi"), noinline]] static i32 add_seven(in i32 value) {
    return value + 7i32;
}

[[noinline]] static i32 add_nine(in i32 value) {
    return value + 9i32;
}

[[abi("stack_result_abi"), noinline]] static i32 add_eleven(in i32 value) {
    return value + 11i32;
}

[[abi("memory_result_abi"), noinline]]
static struct generic_memory_pair make_memory_pair(in u32 value) {
    struct generic_memory_pair result = {
        (u64)value + 17u64, (u64)value + 23u64
    };
    return result;
}

[[abi("odd_abi")]] static T assertion_register<T>(in T value) { return value; }
[[abi("stack_result_abi")]] static T assertion_stack<T>(in T value) { return value; }
[[abi("memory_result_abi")]]
static struct generic_memory_pair assertion_memory<T>(in T value) {
    struct generic_memory_pair result = { (u64)value, (u64)value + 1u64 };
    return result;
}
$::static_assert(assertion_register(9i32) == 9i32,
    "assertion instantiates arbitrary-register result interface");
$::static_assert(assertion_stack(14i32) == 14i32,
    "assertion instantiates stack-result interface");
$::static_assert(assertion_memory(22u32).low == 22u64,
    "assertion instantiates indirect-memory result interface");

[[abi("cross")]] global i32 generic_angle_custom_abi_entry() {
    odd_callback callback = add_seven;
    odd_callback inferred = copy(callback);
    odd_callback explicit_type = copy<odd_callback>(callback);
    stack_callback stacked = copy<stack_callback>(add_eleven);
    stack_callback inferred_stack = copy(stacked);
    stack_callback late_stack = copy_trailing(stacked);
    memory_callback memory = make_memory_pair;
    memory_callback inferred_memory = copy(memory);
    memory_callback late_memory = copy_interleaved(memory);
    struct generic_memory_pair pair = inferred_memory(5u32);
    struct generic_memory_pair inferred_pair = invoke_memory(memory, 7u32);
    struct generic_memory_pair late_pair = late_memory(9u32);
    struct generic_memory_pair copied_pair = copy_trailing(late_pair);
    default_callback selected_default = add_nine;
    default_callback default_inferred = copy(selected_default);
    canonical_callback canonical = copy(alias_add_four);
    alias_callback alias = canonical;
    // Equal model identities must not introduce a fresh adapter address.
    if (canonical != alias_add_four || alias != &alias_add_four) return 3i32;
    return inferred(34i32) == 41i32 &&
           explicit_type(35i32) == 42i32 &&
           inferred_stack(30i32) == 41i32 &&
           invoke_stack(add_eleven, 31i32) == 42i32 &&
           late_stack(32i32) == 43i32 &&
           pair.low == 22u64 && pair.high == 28u64 &&
           inferred_pair.low == 24u64 && inferred_pair.high == 30u64 &&
           copied_pair.low == 26u64 && copied_pair.high == 32u64 &&
           default_inferred(36i32) == 45i32 && alias(37i32) == 41i32 ? 1i32 : 2i32;
}
