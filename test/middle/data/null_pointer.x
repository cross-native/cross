// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct Node { u32 value; struct Node *next; };
static struct Node direct = {7u32, 0};
static struct Node array[2] = {{11u32, 0u128}, {.next = (23u32 - 23u32), .value = 13u32}};
static struct Node *scalar = 0;
static struct Node *computed = sizeof(struct Node *) - sizeof(struct Node *);
typedef void (*Callback)();
static Callback callback = 0;
static u32 zero() { return 0u32; }
static struct Node called = {17u32, zero()};
static union Alternative { struct Node *pointer; uptr integer; } choice = {.pointer = -0};
[[macro]] static $::meta::tokens generated(in $::meta::tokens input) {
    return $::quote { namespace Generated { static struct Node saved = {19u32, 0}; } };
}
generated!{}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { namespace Copied { $::unquote($::syntax::node(input, "value")) } };
}
syntax Copy : item { prefix "copy_null"; match value:declaration; expand copy; }
syntax Copy;
copy_null static struct Node copied = {23u32, 0u64};

[[noinline]] static struct Node read() { return direct; }
enum Zero [[underlying(u32)]] { zero_value = 0u32 };
[[noinline]] static u32 null_checks(in struct Node *pointer) {
    u32 result = pointer == 0 ? 1u32 : 0u32;
    result += 0u128 == pointer ? 2u32 : 0u32;
    result += pointer != (9u32 - 9u32) ? 0u32 : 4u32;
    result += (sizeof(pointer) - sizeof(pointer)) != pointer ? 0u32 : 8u32;
    result += pointer == zero_value ? 16u32 : 0u32;
    result += pointer == $::eval(zero()) ? 32u32 : 0u32;
    return result;
}
[[noinline]] static u32 callback_checks(in Callback pointer) {
    return (pointer == 0u8 && 0u64 == pointer && !(pointer != 0) && !(0 != pointer)) ? 1u32 : 0u32;
}
static void target() {}
static u32 pointer_reads;
static u32 cast_reads;
[[noinline]] static uptr runtime_zero() {
    ++cast_reads;
    return 0uptr;
}
[[noinline]] static struct Node *next_pointer() {
    ++pointer_reads;
    return scalar;
}
static bool meta_null(in u8 *value) { return value == 0; }
[[macro]] static $::meta::tokens is_null(in $::meta::tokens input) {
    return $::quote { ($::unquote(input)) == 0 };
}
[[syntax_expander]] static $::meta::tokens expand_null(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) != (13u32 - 13u32) };
}
syntax NullCheck : expression { prefix "nonnull"; match "(" value:expr ")"; expand expand_null; }
syntax NullCheck;
[[macro]] static $::meta::tokens null_cast(in $::meta::tokens input) {
    return $::quote { (Callback)($::unquote(input)) };
}
[[syntax_expander]] static $::meta::tokens copy_cast(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "function")) };
}
syntax CopyCast : item { prefix "copy_cast"; match function:function_def; expand copy_cast; }
syntax CopyCast;
copy_cast [[noinline]] static Callback copied_null() { return (Callback)(11u16 - 11u16); }
[[noinline]] static Callback narrow_null<T>(in T unused) { return (Callback)(sizeof(T) - sizeof(unused)); }
[[macro]] static $::meta::tokens meta_checks(in $::meta::tokens input) {
    $::meta::buffer bytes = $::meta::alloc(8uptr);
    u8 *pointer = $::meta::data(bytes);
    u8 *empty = 0;
    if (pointer == 0 || 0u64 == pointer || empty != 0 || 0u32 != empty)
        return $::quote { 0u32 };
    empty = pointer;
    empty = 0;
    if (!meta_null(0) || meta_null(pointer) || !meta_null(empty))
        return $::quote { 0u32 };
    return $::quote { 1u32 };
}
$::static_assert(meta_checks!{} == 1u32, "translation-only null comparisons");
#ifdef CUSTOM_NULL_ABI
[[noinline, abi("stack_result_abi")]] static struct Node stack_result() { return array[0u32]; }
[[noinline, abi("memory_result_abi")]] static struct Node memory_result() { return array[1u32]; }
#endif
#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    Callback effectful = (Callback)$::runtime(runtime_zero());
    Callback narrow = (Callback)0u8;
    Callback signed_zero = (Callback)-0i16;
    Callback wide = (Callback)0u128;
    struct Node *object = (struct Node *)zero_value;
    if (effectful != 0 || cast_reads != 1u32 ||
        narrow != 0 || signed_zero != 0 || wide != 0 || object != 0 ||
        null_cast!(0u32) != 0 || copied_null() != 0 || narrow_null(3u16) != 0 ||
        (Callback)$::eval(zero()) != 0 || (struct Node *)(19u32 - 19u32) != 0)
        return 7u32;
    static struct Node local = {29u32, 0};
    struct Node fetched = read();
    if (!(next_pointer() == 0) || !(0 == next_pointer()) || pointer_reads != 2u32)
        return 6u32;
    if (null_checks(scalar) != 63u32 || null_checks(&direct) != 0u32 ||
        callback_checks(callback) != 1u32 || callback_checks(&target) != 0u32 ||
        !is_null!(scalar) || !nonnull(&direct)) return 5u32;
    if (fetched.value != 7u32 || fetched.next != (struct Node *)0uptr ||
        scalar != (struct Node *)0uptr || computed != (struct Node *)0uptr || callback != (Callback)0uptr)
        return 1u32;
    if (array[0u32].next != (struct Node *)0uptr || array[1u32].next != (struct Node *)0uptr ||
        called.next != (struct Node *)0uptr || choice.pointer != (struct Node *)0uptr)
        return 2u32;
    if (Generated::saved.next != (struct Node *)0uptr || Generated::saved.value != 19u32 ||
        Copied::copied.next != (struct Node *)0uptr || Copied::copied.value != 23u32 ||
        local.next != (struct Node *)0uptr || local.value != 29u32)
        return 3u32;
#ifdef CUSTOM_NULL_ABI
    struct Node stacked = stack_result();
    struct Node indirect = memory_result();
    if (stacked.next != (struct Node *)0uptr || stacked.value != 11u32 ||
        indirect.next != (struct Node *)0uptr || indirect.value != 13u32)
        return 4u32;
#endif
    return 61u32;
}
