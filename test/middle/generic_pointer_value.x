// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace pointer_values {
    global u32 numbers[4] = { 11u32, 23u32, 37u32, 49u32 };
    struct record { u8 tag; uptr count; u32 values[2]; };
    global struct record object = { 3u8, 5uptr, { 61u32, 73u32 } };
}
using pointer_values;

[[generic(const u32 *P), noinline]]
#ifdef PRIVATE_POINTER_GENERIC
static const u32 *pointer_identity() { return P; }
#else
global const u32 *pointer_identity() { return P; }
#endif

[[generic(const u8 *P), noinline]]
static const u8 *string_identity() { return P; }

[[generic(T, T *P), noinline]]
static T *typed_identity() { return P; }

[[generic(const u32 *P), noinline]]
static const u32 *pointer_forward() { return pointer_identity::<P>(); }

[[generic(const u32 *P), noinline, link_name("explicit_pointer")]]
#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global const u32 *explicit_pointer() { return P; }

[[generic(const u32 *P), noinline]]
static const u32 *static_pointer() {
    static const u32 *cached = P;
    return cached;
}

[[generic(const u32 *P), noinline]]
static u32 instance_counter() {
    static u32 count = 0u32;
    count += 1u32;
    return count;
}

typedef u32 (*Callback)(in u32 value);
#ifdef CUSTOM_POINTER_ABI
[[abi(HOST_ABI)]]
#endif
[[noinline]]
static u32 increment(in u32 value) { return value + 9u32; }

[[generic(Callback F), noinline]]
static u32 invoke(in u32 value) { return F(value); }

#ifdef CUSTOM_POINTER_ABI
[[generic(const u32 *P), abi("odd_abi"), noinline]]
static const u32 *register_result() { return P; }

[[generic(const u32 *P), abi("stack_result_abi"), noinline]]
static const u32 *stack_result() { return P; }

struct pointer_pair { const u32 *first; const u32 *second; };
[[generic(const u32 *P), abi("memory_result_abi"), noinline]]
static struct pointer_pair memory_result() {
    struct pointer_pair result = { P, P + 1 };
    return result;
}
#endif

[[link_name("generic_pointer_entry")]]
#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global i32 generic_pointer_entry() {
    static u32 local_values[2] = { 83u32, 97u32 };
    const u32 *local = pointer_identity::<local_values + 1>();
    const u8 *string = string_identity::<"pointer">();
    u32 *typed = typed_identity::<u32, &pointer_values::numbers[1]>();
    const u32 *first = pointer_identity::<numbers + (1 + 1)>();
    const u32 *same = pointer_identity::<&pointer_values::numbers[2]>();
    const u32 *start = pointer_forward::<numbers>();
    const u32 *member = pointer_identity::<&object.values[1]>();
    const u32 *conditional = pointer_identity::<(1 ? numbers + 2 : numbers)>();
    const u32 *null = pointer_forward::<0>();
    const u32 *absolute = pointer_identity::<(const u32 *)0x40001000uptr>();
    const u32 *back = pointer_identity::<(numbers + 3) - 1>();
    const u32 *negative = pointer_identity::<numbers + 3 + (-1)>();
    const u32 *layout = pointer_identity::<numbers + (sizeof(uptr) / sizeof(uptr))>();
    const u32 *past = pointer_identity::<numbers + 4>();
    const u32 *cached = static_pointer::<numbers + 1>();
    const u32 *cached_absolute = static_pointer::<(const u32 *)0x40001000uptr>();
    const u32 *exported = explicit_pointer::<numbers>();
    if (*local != 97u32 || string[0] != 112u8 || string[6] != 114u8 || *typed != 23u32) return 0;
    if (first != same || *first != 37u32 || *start != 11u32) return 0;
    if (*member != 73u32 || conditional != first || back != first) return 0;
    if (negative != first || *layout != 23u32 || past != (const u32 *)(numbers + 4)) return 0;
    if (null != (const u32 *)0uptr || (uptr)absolute != 0x40001000uptr) return 0;
    if (*cached != 23u32 || cached_absolute != absolute || exported != start) return 0;
    if (instance_counter::<numbers + (1 + 1)>() != 1u32 ||
        instance_counter::<&pointer_values::numbers[2]>() != 2u32 ||
        instance_counter::<numbers + 1>() != 1u32) return 0;
    if (invoke::<increment>(13u32) != 22u32) return 0;
#ifdef CUSTOM_POINTER_ABI
    const u32 *reg = register_result::<numbers + 1>();
    const u32 *stacked = stack_result::<numbers + 2>();
    struct pointer_pair memory = memory_result::<numbers>();
    if (*reg != 23u32 || *stacked != 37u32 ||
        *memory.first != 11u32 || *memory.second != 23u32) return 0;
#endif
    return 1;
}
