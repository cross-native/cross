// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[macro]] static $::meta::tokens part(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "function")) };
}
syntax Copy : item { prefix "copy"; match function:function_def; expand copy; }
syntax Copy;

[[noinline]] static const u32 scalar(in u32 value) { return value + 3u32; }
[[noinline]] static volatile u32 observed(in u32 value) { return value + 7u32; }
[[noinline]] static part!(const) T generic<T>(in T value) { return value; }
copy [[noinline]] static const volatile i32 signed_result(in i32 value) { return value - 5i32; }
copy [[noinline]]
#ifdef CUSTOM_NULL_ABI
[[abi("test_sysv")]]
#endif
static const f32 floating_result(in f32 value) { return value + 2.0f32; }
enum Number [[underlying(u16)]] { chosen = 401 };
copy [[noinline]] static const enum Number enum_result() { return chosen; }
struct Pair { u32 first; u32 second; };
copy [[noinline]] static const struct Pair record_result(in u32 value) {
    struct Pair result = { value, value + 1u32 };
    return result;
}
copy [[noinline]] static volatile struct Pair observed_record(in u32 value) {
    struct Pair result = { value + 2u32, value + 3u32 };
    return result;
}
[[noinline]] static u32 *const pointer_result(in u32 *value) { return value; }
[[noinline]] static u32 *volatile observed_pointer(in u32 *value) { return value; }
typedef const u32 (*Callback)(in u32);
typedef volatile u32 (*ObservedCallback)(in u32);
[[noinline]] static u32 call_result(in Callback callback, in ObservedCallback observer) {
    return callback(31u32) + observer(37u32);
}
global void result_label_owner() { global label anchor: ; }
[[noinline]] static const label label_result(in label value) { return value; }
static u32 calls;
[[noinline]] static const volatile u32 effectful_result() { ++calls; return calls; }

#ifdef TEST_VECTOR_RESULTS
typedef u32 Lanes [[ext_vector_type(4)]];
// The deliberately integer-only odd_abi has no vector result rule.
[[abi("sysv_abi"), noinline]] static const Lanes vector_result(in Lanes value) { return value + 3u32; }
[[abi("sysv_abi"), noinline]] static volatile Lanes observed_vector(in Lanes value) { return value + 5u32; }
#endif

#ifdef CUSTOM_NULL_ABI
struct Packet { u64 first; u64 second; };
typedef const uptr (*StackCallback)(in u32) [[abi("stack_result_abi")]];
typedef volatile struct Packet (*MemoryCallback)(in u32) [[abi("memory_result_abi")]];
[[abi("stack_result_abi"), noinline]] static const uptr stack_result(in u32 value) {
    return (uptr)value + 1uptr;
}
[[abi("memory_result_abi"), noinline]] static volatile struct Packet memory_result(in u32 value) {
    struct Packet result = { (u64)value, 43u64 };
    return result;
}
[[noinline]] static u32 custom_callbacks(in StackCallback stacked, in MemoryCallback memory) {
    struct Packet result = memory(41u32);
    return stacked(39u32) == 40uptr && result.first == 41u64 && result.second == 43u64;
}
#endif

$::static_assert(scalar(300u32) == 303u32 && generic(511u32) == 511u32,
                  "qualified result evaluation changed");
#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if ($::runtime(scalar(17u32)) != 20u32 || $::runtime(observed(19u32)) != 26u32) return 1u32;
    if ($::runtime(generic(300u32)) != 300u32 || $::runtime(signed_result(-13i32)) != -18i32) return 2u32;
    if ($::runtime(floating_result(3.5f32)) != 5.5f32 || $::runtime(enum_result()) != chosen) return 3u32;
    struct Pair pair = $::runtime(record_result(23u32));
    struct Pair observed_pair = $::runtime(observed_record(29u32));
    if (pair.first != 23u32 || pair.second != 24u32 || observed_pair.first != 31u32 || observed_pair.second != 32u32) return 4u32;
    u32 local = 47u32;
    if ($::runtime(pointer_result(&local)) != &local || $::runtime(observed_pointer(&local)) != &local) return 5u32;
    if ($::runtime(pointer_result(&local)) - &local != 0iptr ||
        &local - $::runtime(observed_pointer(&local)) != 0iptr) return 10u32;
    u32 *selected = local == 47u32 ? $::runtime(pointer_result(&local)) : &local;
    u32 *other = local == 47u32 ? &local : $::runtime(observed_pointer(&local));
    if (selected != &local || other != &local) return 11u32;
    u32 values[4] = { 1u32, 2u32, 3u32, 4u32 };
    if ($::runtime(pointer_result(&values[2])) - values != 2iptr ||
        values - $::runtime(observed_pointer(&values[3])) != -3iptr) return 12u32;
    label selected_label = local == 47u32
        ? $::runtime(label_result(result_label_owner::anchor)) : result_label_owner::anchor;
    if (selected_label != result_label_owner::anchor ||
        $::runtime(label_result(result_label_owner::anchor)) != result_label_owner::anchor) return 13u32;
    if ($::runtime(effectful_result()) != 1u32 || $::runtime(effectful_result()) != 2u32 || calls != 2u32) return 14u32;
    if ($::runtime(call_result(scalar, observed)) != 78u32) return 6u32;
#ifdef TEST_VECTOR_RESULTS
    Lanes lanes = 11u32;
    Lanes first = $::runtime(vector_result(lanes));
    Lanes second = $::runtime(observed_vector(lanes));
    if (first[0] != 14u32 || first[3] != 14u32 || second[0] != 16u32 || second[3] != 16u32) return 7u32;
#endif
#ifdef CUSTOM_NULL_ABI
    if ($::runtime(stack_result(53u32)) != 54uptr) return 8u32;
    struct Packet packet = $::runtime(memory_result(59u32));
    if (packet.first != 59u64 || packet.second != 43u64 ||
        !$::runtime(custom_callbacks(stack_result, memory_result))) return 9u32;
#endif
    return 61u32;
}
