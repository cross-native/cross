// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaTypeValidation {
    static volatile u32 runtime_state;
    [[runtime_only, noinline]] static u32 runtime_value() { return runtime_state; }
    static u32 ordinary(in bool execute) {
        if (execute) return runtime_value();
        return 83u32;
    }
    static T copied<T>(in T value) { return value; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        // Object validation must not demand the pointee's layout.
        struct Incomplete;
        struct Incomplete *opaque = (struct Incomplete *)0uptr;
        void *untyped = (void *)opaque;
        if (opaque || untyped) return $::quote { 0u32 };
        // Const applies to the pointer cell, not to the pointed-to object.
        u32 scalar = 2u32;
        u32 *const pointer = &scalar;
        (*pointer) += 3u32;
        u32 [[ext_vector_type(4)]] lanes = 1u32;
        (lanes[0uptr]) = scalar;
        struct Holder { u32 values[2]; } holder = {{7u32, 9u32}};
        struct Holder *const record = &holder;
        ++record->values[1uptr];
        if (scalar != 5u32 || lanes[0uptr] != 5u32 || holder.values[1uptr] != 10u32)
            return $::quote { 0u32 };
        struct Holder holders[1] = {{{11u32, 13u32}}};
        holders->values[1uptr] += 2u32;
        const struct Holder readonly[1] = {{{17u32, 19u32}}};
        if (copied(holders->values[0uptr]) != 11u32 || holders->values[1uptr] != 15u32 ||
            copied(readonly->values[0uptr]) != 17u32 || (*holders).values[1uptr] != 15u32)
            return $::quote { 0u32 };
        // Address checking and generic deduction retain array types under &.
        if (sizeof(*copied(&holders)) != sizeof(holders) ||
            sizeof(*copied(&"ab\0c")) != 5uptr || sizeof("ab\0c") != 5uptr)
            return $::quote { 0u32 };
        typedef u32 Function();
        // Valid layout queries are unevaluated even when the operand would
        // read runtime state, mutate a cell, or dereference a null pointer.
        if (sizeof(runtime_value()) != sizeof(u32) || sizeof(++scalar) != sizeof(u32) ||
            $::alignof(++scalar) != $::alignof(u32) ||
            sizeof(*((u32 *)0uptr)) != sizeof(u32) || scalar != 5u32 ||
            sizeof(struct Incomplete *) == 0uptr || sizeof(Function *) == 0uptr ||
            $::alignof(struct Incomplete *) == 0uptr || $::alignof(Function *) == 0uptr)
            return $::quote { 0u32 };
        u32 captured[scalar];
        if (sizeof(captured) != (uptr)scalar * sizeof(u32)) return $::quote { 0u32 };
        if ((bool)0u8) {
            // These have valid source types, but would fail if executed by an
            // expansion. Type checking must not run the unreachable effects.
            u32 (*callback)() = runtime_value;
            &runtime_value;
            &*callback;
            &scalar;
            &holders;
            &holders->values[0uptr];
            &lanes[0uptr];
            &*((struct Incomplete *)0uptr);
            &("ab\0c");
            u32 indirect = callback();
            u32 value = runtime_value();
            value += runtime_state;
            volatile u32 cell = value;
            cell += 1u32;
            u32 divided = value / 0u32;
            $::meta::buffer data = $::meta::alloc((uptr)divided);
            $::meta::bytes invalid = $::meta::freeze(data, 1uptr);
            if ($::meta::len(invalid) != 0uptr) return $::quote { 0u32 };
        }
        if (ordinary((bool)0u8) != 83u32) return $::quote { 0u32 };
        return input;
    }
    static T generic<T>(in T input) {
        if ((bool)0u8) {
            T unselected = input;
            unselected = input;
        }
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return helper(generic(input));
    }
    [[noinline]] static u32 run() { return apply!(83u32); }
}
