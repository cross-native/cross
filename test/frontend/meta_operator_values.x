// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace SourceOperators {
    typedef u32 Words [[ext_vector_type(4)]];
    typedef f32 Floats [[ext_vector_type(4)]];
    struct Cell { u16 value; };
    struct Incomplete;
    typedef struct Incomplete OpenRow[2];
    static T identity<T>(in T value) { return value; }
    [[noinline]] static bool incomplete_designators() {
        u32 object = 7u32;
        void *opaque = &object;
        struct Incomplete *record = opaque;
        OpenRow *rows = opaque;
        return &(*record) == record && &(*rows) == rows;
    }
    [[noinline]] static u32 pointer_values(in u32 select) {
        u32 values[2] = {11u32, 17u32};
        u32 *mutable = values;
        const u32 *readonly = values + 1uptr;
        const u32 *const *nested = select ? &mutable : &readonly;
        const u32 *const *inferred = identity(select ? &mutable : &readonly);
        const u32 *const *reverse = select ? &readonly : &mutable;
        if (**nested != (select ? 11u32 : 17u32) || **inferred != **nested ||
            **reverse != (select ? 17u32 : 11u32)) return 0u32;
        void *opaque = mutable;
        const void *mixed = select ? readonly : opaque;
        const void *other = select ? opaque : readonly;
        if (mixed != (select ? readonly : mutable) || other != (select ? mutable : readonly)) return 0u32;
        u32 *null_first = select ? 0u32 : mutable;
        u32 *null_last = select ? mutable : 0uptr;
        if ((null_first == 0uptr) != (select != 0u32) || (null_last == 0uptr) != (select == 0u32)) return 0u32;
        if ((null_first ? 1u32 : 0u32) != (select ? 0u32 : 1u32) ||
            (bool)null_last != (select != 0u32) || !null_last != (select == 0u32)) return 0u32;
        const u32 (*row)[2] = &values;
        const u32 (*joined_row)[2] = select ? &values : row;
        if ((*joined_row)[1uptr] != 17u32) return 0u32;
        return 29u32;
    }
    static $::meta::tokens verify() {
        u32 bits = 0x35u32;
        bits &= 0x0fu32; bits |= 0x10u32; bits ^= 1u32;
        bits <<= 1u32; bits >>= 2u32; bits %= 7u32;
        i32 signed_value = -9i32;
        signed_value = -signed_value + 3i32;
        f64 number = 1.5f64;
        number *= 4u8; number /= 2u16; number -= 1u32;
        Words lanes = 1u32;
        lanes[1u32] = 2u32; lanes[2u32] = 3u32; lanes[3u32] = 4u32;
        lanes <<= 1u32; lanes &= 7u32; lanes += 1u32;
        Floats floats = 1.5f32;
        floats *= 2u8; floats += 1u16;
        u32 values[3] = {3u32, 5u32, 7u32};
        u32 *pointer = 1uptr + values;
        const u32 *readonly = values;
        const void *opaque = readonly;
        const u32 *recovered = opaque;
        const u32 *const *nested = &readonly;
        u32 *mutable = values;
        const u32 *const *qualified_nested = &mutable;
        f64 widened = (u8)257u32;
        Floats converted = lanes;
        ++pointer; pointer -= 1uptr;
        struct Cell cells[2] = {{11u16}, {13u16}};
        struct Cell *cell = cells + 1uptr;
        u32 rows[2][2] = {{17u32, 19u32}, {23u32, 29u32}};
        u32 (*row)[2] = rows;
        ++row;
        Words comparison = lanes > 2u32;
        Words scalar_comparison = 2u32 < lanes;
        Words negated = !lanes;
        Words selected = number ? lanes : 9u32;
        Floats selected_float = 0u32 ? 2.0f32 : floats;
        u32 *selected_pointer = signed_value ? pointer : values;
        bool selected_numeric = (0u32 ? 1u32 : 1.5f64) > 1u32;
        u32 effects = 0u32;
        bool short_and = 0u32 && ++effects;
        bool short_or = 1u32 || ++effects;
        if (0u32) {
            volatile u32 external;
            external += 1u32;
            // Operand constraints are checked without executing effects or UB.
            i32 never = 1i32 / 0i32;
        }
        if (bits != 3u32 || signed_value != 12i32 || number != 2.0f64 ||
            lanes[0u32] + lanes[1u32] + lanes[2u32] + lanes[3u32] != 16u32 ||
            floats[0u32] != 4.0f32 || floats[3u32] != 4.0f32 ||
            *(pointer - 1uptr) != 3u32 || pointer - values != 1iptr || pointer - readonly != 1iptr ||
            cell->value != 13u16 || (*row)[1u32] != 29u32 ||
            *recovered != 3u32 || **nested != 3u32 || **qualified_nested != 3u32 || widened != 1.0f64 ||
            converted[0u32] != 3.0f32 || converted[3u32] != 1.0f32 ||
            comparison[0u32] != 0xffffffffu32 || comparison[3u32] != 0u32 ||
            scalar_comparison[1u32] != comparison[1u32] || negated[2u32] != 0u32 ||
            selected[2u32] != lanes[2u32] || selected_float[0u32] != 4.0f32 ||
            selected_pointer != pointer || !(values < pointer) || !(pointer <= values + 2uptr) ||
            !selected_numeric || short_and || !short_or || effects != 0u32 ||
            identity((u8)3u32 + (u16)4u32) != 7i32 ||
            sizeof(~((u8)1u32)) != sizeof(i32)) return $::quote { 0u32 };
        if (pointer_values(0u32) != 29u32 || pointer_values(1u32) != 29u32)
            return $::quote {0u32};
        if (!incomplete_designators()) return $::quote {0u32};
        return $::quote { 61u32 };
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return verify(); }
    [[noinline]] static u32 runtime(in u32 input) {
        u32 values[3] = {3u32, input, 7u32};
        u32 *pointer = values;
        pointer += 2uptr; --pointer;
        u32 *selected = input ? pointer : values;
        if (selected != pointer || !(values < selected) || !(selected <= values + 2uptr) ||
            !((input ? input : 1.5f64) >= 5u32)) return 0u32;
        return *pointer + (u32)(pointer - values);
    }
    [[noinline]] static u32 pointer_designators(in u32 input) {
        u32 values[2] = {input, 17u32};
        void *opaque = &values;
        struct Incomplete *record = opaque;
        OpenRow *open = opaque;
        u32 (*function)(in u32 value) = &runtime;
        if (&(*record) != record || &(*open) != open || &*function != function)
            return 0u32;
        u32 (*rows)[2] = &values;
        if ((*rows)[1uptr] != 17u32 || (&rows[0uptr]) != rows ||
            (*function)(input) != input + 1u32) return 0u32;
        return 31u32;
    }
}
#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return SourceOperators::apply!() == 61u32 && SourceOperators::runtime(5u32) == 6u32 &&
        SourceOperators::pointer_values(0u32) == 29u32 && SourceOperators::pointer_values(1u32) == 29u32
        && SourceOperators::pointer_designators(5u32) == 31u32
        && SourceOperators::incomplete_designators()
        ? 61u32 : 0u32;
}
