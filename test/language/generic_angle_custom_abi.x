// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*odd_callback)(in i32 value) [[abi("odd_abi")]];
typedef i32 (*stack_callback)(in i32 value) [[abi("stack_result_abi")]];
struct generic_memory_pair { u64 low; u64 high; };
typedef struct generic_memory_pair (*memory_callback)(in u32 value)
    [[abi("memory_result_abi")]];
typedef i32 (*default_callback)(in i32 value);

static T copy<T>(in T value) {
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

[[abi("cross")]] global i32 generic_angle_custom_abi_entry() {
    odd_callback callback = add_seven;
    odd_callback inferred = copy(callback);
    odd_callback explicit_type = copy<odd_callback>(callback);
    stack_callback stacked = copy<stack_callback>(add_eleven);
    stack_callback inferred_stack = copy(stacked);
    memory_callback memory = make_memory_pair;
    memory_callback inferred_memory = copy(memory);
    struct generic_memory_pair pair = inferred_memory(5u32);
    struct generic_memory_pair inferred_pair = invoke_memory(memory, 7u32);
    default_callback selected_default = add_nine;
    default_callback default_inferred = copy(selected_default);
    return inferred(34i32) == 41i32 &&
           explicit_type(35i32) == 42i32 &&
           inferred_stack(30i32) == 41i32 &&
           invoke_stack(add_eleven, 31i32) == 42i32 &&
           pair.low == 22u64 && pair.high == 28u64 &&
           inferred_pair.low == 24u64 && inferred_pair.high == 30u64 &&
           default_inferred(36i32) == 45i32 ? 1i32 : 2i32;
}
