// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
typedef uptr (*Callback)(in u32);
[[runtime_only]] static Callback factory();
static Callback saved;

[[eval_only]] static uptr inspect(in Callback pointer) {
    uptr first = sizeof(pointer(1u32));
    uptr second = $::alignof((*pointer)(2u32));
    return first + second + (0u32 ? pointer(3u32) : 5uptr);
}
$::static_assert(inspect((Callback)0uptr) == sizeof(uptr) + $::alignof(uptr) + 5uptr,
    "type-only callback queries");
$::static_assert(sizeof(factory()(4u32)) == sizeof(uptr), "unexecuted factory");
$::static_assert(sizeof((*saved)(5u32)) == sizeof(uptr), "no static pointer read");
$::static_assert((1u32 || ((Callback)0uptr)(6u32)) == 1u32, "short circuit");

static $::meta::tokens helper(in $::meta::tokens input) {
    Callback pointer = 0u32;
    $::static_assert(sizeof(pointer(1u32)) == sizeof(uptr), "meta helper call type");
    $::static_assert((0u32 && (*pointer)(2u32)) == 0u32, "meta helper short circuit");
    return input;
}
typedef $::meta::tokens Tokens;
typedef $::meta::syntax_match ExpansionInput;
[[macro, noinline]] static Tokens macro_query(Tokens const input) {
    return helper(input);
}
static Tokens (copy)(const ExpansionInput input) [[syntax_expander, always_inline]] {
    Callback pointer = 0u32;
    $::static_assert($::alignof(pointer(1u32)) == $::alignof(uptr), "expander call type");
    return helper($::quote { $::unquote($::syntax::node(input, "function")) });
}
syntax Copy : item { prefix "copy"; match function:function_def; expand copy; }
syntax Copy;
copy static uptr copied(in Callback pointer) {
    return sizeof(pointer(7u32)) + (0u32 ? pointer(8u32) : 9uptr);
}
static uptr ordinary(in u32 value) { return (uptr)value + 3uptr; }

#ifdef CUSTOM_NULL_ABI
struct Packet { u64 first; u64 second; };
typedef uptr (*StackCallback)(in u32) [[abi("stack_result_abi")]];
typedef struct Packet (*MemoryCallback)(in u32) [[abi("memory_result_abi")]];
[[abi("stack_result_abi")]] static uptr stack_result(in u32 value) { return (uptr)value + 1uptr; }
[[abi("memory_result_abi")]] static struct Packet memory_result(in u32 value) {
    struct Packet result = { (u64)value, 19u64 };
    return result;
}
$::static_assert(sizeof(((StackCallback)0uptr)(1u32)) == sizeof(uptr), "stack result query");
$::static_assert(sizeof(((MemoryCallback)0uptr)(1u32)) == sizeof(struct Packet), "memory result query");
#endif

#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (copied((Callback)0u32) != sizeof(uptr) + 9uptr) return 1u32;
    if (macro_query!(sizeof(factory()(10u32))) != sizeof(uptr)) return 2u32;
    if ($::eval(inspect((Callback)0uptr)) != sizeof(uptr) + $::alignof(uptr) + 5uptr) return 3u32;
    Callback runtime_pointer = ordinary;
    if ($::runtime(runtime_pointer(8u32)) != 11uptr) return 4u32;
#ifdef CUSTOM_NULL_ABI
    StackCallback stack_pointer = stack_result;
    MemoryCallback memory_pointer = memory_result;
    if ($::runtime(stack_pointer(12u32)) != 13uptr) return 5u32;
    struct Packet packet = $::runtime(memory_pointer(17u32));
    if (packet.first != 17u64 || packet.second != 19u64) return 6u32;
#endif
    return 61u32;
}
