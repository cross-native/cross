// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaOperators {
    enum Count [[underlying(u32)]] { One = 1u32, Two = 2u32 };
    struct Box { u64 value; };
    static T copy<T>(in T value) { return value; }
    static uptr width<T>(in T value) { return sizeof(T); }

    // Top-level const does not create a different adjusted operand signature.
    // The selected operator's body must prepare its own generic helper calls.
    [[operator("+"), noinline]]
#ifdef CUSTOM_SYNTAX_ABI
    [[abi("stack_result_abi")]]
#endif
    static enum Count add(in const enum Count left, in enum Count right) {
        return copy((enum Count)((u32)left + (u32)right + 10u32));
    }
    [[operator("+"), noinline]] static u32 add_small(in enum Count left, in u16 right) {
        return (u32)left + (u32)right + 20u32;
    }
    [[operator("+"), noinline]] static u32 add_promoted(in enum Count left, in i32 right) {
        return (u32)left + (u32)right + 30u32;
    }
    [[operator("+"), noinline]]
#ifdef CUSTOM_SYNTAX_ABI
    [[abi("memory_result_abi")]]
#endif
    static struct Box combine(in const struct Box left, in struct Box right) {
        struct Box result = { copy(left.value + right.value + 40u64) };
        return result;
    }
    [[operator("+"), noinline]] static u64 unwrap(in const struct Box value) {
        return copy(value.value);
    }
    [[operator("!"), noinline]] static bool zero(in struct Box value) {
        return value.value == 0u64;
    }
    enum Prepared [[underlying(u32)]] { Thirteen = (u32)(One + Two) };

    static $::meta::tokens check() {
        const enum Count first = One;
        enum Count values[2] = {One, Two};
        struct Box boxes[2] = {{3u64}, {4u64}};
        const struct Box *selected = &boxes[0uptr];
        struct Box result = boxes[0uptr] + boxes[1uptr];
        if ((u32)(first + values[1uptr]) != 13u32 ||
            One + 2u16 != 23u32 || One + (1u8 + 1u8) != 33u32 ||
            (u32)Thirteen != 13u32 || result.value != 47u64 ||
            +*selected != 3u64 || copy(+result) != 47u64 || width(+result) != sizeof(u64) ||
            !result || !((bool)1u8)) return $::quote { 0u32 };
        return $::quote { 73u32 };
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        struct Box left = {5u64}, right = {6u64};
        // Direct bodies and early inferred generic arguments use the same
        // binding path as ordinary helpers, including a changed result type.
        if (copy(+(left + right)) != 51u64 || width(+left) != sizeof(u64))
            return $::quote { 0u32 };
        return check();
    }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        struct Box left = {5u64}, right = {6u64};
        if (+(left + right) != 51u64) return $::quote { 0u32 };
        return $::quote { $::unquote($::syntax::node(input, "value")) };
    }
    syntax Bound : expression { prefix "bound_operator"; match "(" value:expr ")"; expand expand; }
    syntax Bound;
    [[noinline]] static u32 runtime(in enum Count first, in enum Count second,
                                   in struct Box left, in struct Box right) {
        struct Box result = left + right;
        if ((u32)(first + second) != 13u32 || +result != 47u64 || !result ||
            copy(+result) != 47u64 || width(+result) != sizeof(u64)) return 0u32;
        return 73u32;
    }
    [[noinline]] static u32 run() {
        struct Box left = {3u64}, right = {4u64};
        if (apply!() != 73u32 || bound_operator(73u32) != 73u32 ||
            runtime(One, Two, left, right) != 73u32) return 0u32;
        return 73u32;
    }
}

// A later reached binding is added to the persistent expansion view. It must
// neither be lost nor invalidate the earlier enum's nominal operator identity.
namespace LaterMetaOperators {
    enum Count [[underlying(u32)]] { One = 1u32, Two = 2u32 };
    [[operator("+")]] static u32 add(in enum Count left, in enum Count right) {
        return (u32)left + (u32)right + 50u32;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        if (One + Two != 53u32) return $::quote { 0u32 };
        return input;
    }
    $::static_assert(apply!(1u32), "later expansion operator binding");
}
