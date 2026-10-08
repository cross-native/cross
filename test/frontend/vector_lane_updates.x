// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace VectorLaneUpdates {
    typedef u32 U4 [[ext_vector_type(4)]];
    typedef u8 U16 [[ext_vector_type(16)]];
    typedef f32 F4 [[ext_vector_type(4)]];
    typedef uptr P16 [[vector_size(16)]];
    enum { Boundary = 4uptr };
    struct Cell { U4 lanes; };
    [[packed]] struct Packed { u8 lead; U4 lanes; };
    static U4 global_lanes;
    static volatile U4 volatile_lanes;
    static u32 effects;

    [[noinline]] static struct Cell *base(in struct Cell *value) {
        effects = effects * 10u32 + 1u32;
        return value;
    }
    [[noinline]] static uptr index() {
        effects = effects * 10u32 + 2u32;
        return 2uptr;
    }
    [[noinline]] static u32 source(in struct Cell *value) {
        effects = effects * 10u32 + 3u32;
        value->lanes[1uptr] = 99u32;
        value->lanes[2uptr] = 77u32;
        return 5u32;
    }
    [[noinline]] static u32 dynamic_lane(in U4 *value, in uptr Boundary) {
        return (*value)[Boundary];
    }
    // Runtime-dependent indices stay runtime-dependent even when a same-
    // spelled enum is visible outside the parameter/local scope.
    static $::meta::tokens unchecked_runtime(in $::meta::tokens input) {
        if ((bool)0u8) {
            U4 lanes = 0u32;
            uptr Boundary = 4uptr;
            lanes[Boundary] = 1u32;
        }
        return input;
    }

    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Function : item { prefix "lane_function"; match body:function_def; expand copy; }
    syntax Function;
    lane_function [[noinline]] static u32 run() {
        // Memory lane operations use scalar MIR even on targets without
        // general vector-value instructions or vector ABI transport.
        struct Cell memory_cell;
        memory_cell.lanes[1u32 ? 2u32 : (1u32 / 0u32)] = 9u32;
        uptr Boundary = 2uptr;
        if (memory_cell.lanes[Boundary] != 9u32 ||
            dynamic_lane(&memory_cell.lanes, Boundary) != 9u32) return 0u32;
        effects = 0u32;
        memory_cell.lanes[sizeof(++effects) - 1uptr] = 12u32;
        if (effects != 0u32 || memory_cell.lanes[3uptr] != 12u32) return 0u32;
        memory_cell.lanes[(0u32 && (1u32 / 0u32)) + 1u32] = 13u32;
        if (memory_cell.lanes[1uptr] != 13u32) return 0u32;
        struct WidthCell { P16 lanes; } address_width;
        address_width.lanes[16uptr / sizeof(Boundary) - 1uptr] = 15uptr;
        if (address_width.lanes[16uptr / sizeof(uptr) - 1uptr] != 15uptr) return 0u32;
        memory_cell.lanes[2uptr] = 4u32;
        effects = 0u32;
        if ((base(&memory_cell)->lanes[index()] += source(&memory_cell)) != 9u32 ||
            effects != 123u32 || memory_cell.lanes[1uptr] != 99u32 ||
            memory_cell.lanes[2uptr] != 9u32) return 0u32;
        struct Packed memory_packed;
        memory_packed.lead = 1u8;
        memory_packed.lanes[2uptr] = 3u32;
        if (++memory_packed.lanes[2uptr] != 4u32 || memory_packed.lead != 1u8)
            return 0u32;
        global_lanes[2uptr] = 4u32;
        global_lanes[2uptr] += 3u32;
        if (global_lanes[2uptr]++ != 7u32 || global_lanes[2uptr] != 8u32)
            return 0u32;
        volatile_lanes[1uptr] = 4u32;
        volatile_lanes[1uptr] += 3u32;
        if (volatile_lanes[1uptr]++ != 7u32 || volatile_lanes[1uptr] != 8u32)
            return 0u32;
#if $::has_feature($::feature::fixed_vectors)
        U4 lanes = 10u32;
        uptr next = 1uptr;
        if ((lanes[next++] += (lanes[0uptr] = 7u32)) != 17u32 ||
            next != 2uptr || lanes[0uptr] != 7u32 || lanes[1uptr] != 17u32)
            return 0u32;
        if (lanes[1uptr]++ != 17u32 || ++lanes[1uptr] != 19u32 ||
            lanes[1uptr]-- != 19u32 || --lanes[1uptr] != 17u32) return 0u32;
        if (lanes[(lanes[0uptr] = 2u32)] != 10u32 || lanes[0uptr] != 2u32)
            return 0u32;
        if (lanes[(lanes[0uptr] = 0u32)] != 0u32) return 0u32;
        struct Cell cell = { .lanes = 4u32 };
        effects = 0u32;
        if ((base(&cell)->lanes[index()] += source(&cell)) != 9u32 ||
            effects != 123u32 || cell.lanes[1uptr] != 99u32 ||
            cell.lanes[2uptr] != 9u32) return 0u32;
        U4 array[2] = { 3u32, 6u32 };
        uptr row = 0uptr, column = 1uptr;
        array[row++][column++] *= 4u32;
        if (row != 1uptr || column != 2uptr || array[0uptr][1uptr] != 12u32 ||
            array[1uptr][1uptr] != 6u32) return 0u32;
        U4 *pointer = &lanes;
        (*pointer)[3uptr] /= 2u32;
        if ((*pointer)[3uptr] != 5u32) return 0u32;
        u32 *lane_pointer = &lanes[3uptr];
        *lane_pointer += 2u32;
        if (lanes[3uptr] != 7u32) return 0u32;
        struct Packed packed = { .lead = 1u8, .lanes = 3u32 };
        packed.lanes[2uptr] += 4u32;
        if (packed.lead != 1u8 || packed.lanes[2uptr] != 7u32) return 0u32;
        global_lanes = 4u32;
        global_lanes[2uptr] += 3u32;
        if (global_lanes[2uptr]++ != 7u32 || global_lanes[2uptr] != 8u32)
            return 0u32;
        volatile_lanes[1uptr] = 4u32;
        volatile_lanes[1uptr] += 3u32;
        if (volatile_lanes[1uptr]++ != 7u32 || volatile_lanes[1uptr] != 8u32)
            return 0u32;
        volatile U4 local_volatile = 5u32;
        if (--local_volatile[2uptr] != 4u32 || local_volatile[1uptr] != 5u32)
            return 0u32;
        U16 narrow = 200u8;
        if ((narrow[0uptr] /= 300u16) != 0u8 ||
            (narrow[1uptr] %= 300u16) != 200u8 ||
            (narrow[2uptr] /= 2.5f32) != 80u8) return 0u32;
        narrow[3uptr] = 3u8;
        narrow[3uptr] <<= 7u32;
        if (narrow[3uptr] != 128u8) return 0u32;
        narrow[3uptr] >>= 7u32;
        narrow[3uptr] |= 6u8;
        narrow[3uptr] &= 3u8;
        narrow[3uptr] ^= 1u8;
        narrow[3uptr] -= 1u8;
        if (narrow[3uptr] != 1u8) return 0u32;
        F4 floats = 1.5f32;
        if ((floats[1uptr] *= 2u32) != 3.0f32 ||
            floats[1uptr]++ != 3.0f32 || --floats[1uptr] != 3.0f32)
            return 0u32;
#endif
        return 97u32;
    }
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return VectorLaneUpdates::run() == 97u32 ? 61u32 : 0u32;
}
