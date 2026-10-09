// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross ABI variadic calls and definitions: cross32 with -DGP=u32 and
// cross-n64 with -DGP=u64. Unnamed arguments take the integer and floating
// register positions that the named ones leave, which a definition reads
// through gp_arg_area and fp_arg_area, then the packed stack area at
// overflow_arg_area. Under cross32 an i64 is two words, each in the next
// integer position or stack slot, in memory order. The unnamed argument k of
// each format (1-based) has a known value.

[[runtime_only, noinline]]
global i32 int_value(in u32 k) {
    return 1000i32 * (i32)k - 7777i32;
}

[[runtime_only, noinline]]
global i64 long_value(in u32 k) {
    return ((i64)k << 33) - (i64)k * 12345i64 - 1i64;
}

[[runtime_only, noinline]]
global f64 double_value(in u32 k) {
    return (f64)k + 0.5;
}

[[runtime_only, noinline]]
global i32 small_value(in u32 k) {
    return 3i32 - 7i32 * (i32)k;
}

// Reads the next integer position: a saved register while any remain, else
// the next stack slot of `bytes`.
[[runtime_only, noinline]]
global u64 next_integer(in GP *gp, in u32 *taken, in u32 free,
                        in uptr *cursor, in uptr bytes) {
    u32 index = *taken;
    *taken = index + 1u32;
    if (index < free) return gp[index];
    uptr slot = (*cursor + bytes - 1uptr) & ~(bytes - 1uptr);
    *cursor = slot + bytes;
    return bytes == 8uptr ? *(u64 *)slot : *(u32 *)slot;
}

// Walks the unnamed arguments; returns 0 or a code naming the first wrong
// argument.
[[runtime_only, noinline]]
global u32 cross_walk(in const u8 *format, in GP *gp, in u32 gp_free,
                      in f64 *fp, in u32 fp_free, in void *overflow) {
    u32 one = 1u32;
    bool little = *(const u8 *)(const void *)&one == 1u8;
    uptr cursor = (uptr)overflow;
    u32 gp_taken = 0u32;
    u32 fp_taken = 0u32;
    u32 k = 0u32;
    while (format[k] != 0u8) {
        u32 code = format[k];
        k += 1u32;
        if (code == 'd') {
            f64 value = 0.0;
            if (fp_taken < fp_free) {
                value = fp[fp_taken];
            } else {
                cursor = (cursor + 7uptr) & ~7uptr;
                value = *(f64 *)cursor;
                cursor += 8uptr;
            }
            fp_taken += 1u32;
            if (value != double_value(k)) return 300u32 + k;
        } else if (code == 'l' && sizeof(GP) == 4uptr) {
            u64 first = next_integer(gp, &gp_taken, gp_free, &cursor, 4uptr);
            u64 second = next_integer(gp, &gp_taken, gp_free, &cursor, 4uptr);
            u64 value = little ? (second << 32) | (first & 0xffffffffu64)
                               : (first << 32) | (second & 0xffffffffu64);
            if ((i64)value != long_value(k)) return 200u32 + k;
        } else {
            u64 word = next_integer(gp, &gp_taken, gp_free, &cursor, sizeof(GP));
            if (code == 'l') {
                if ((i64)word != long_value(k)) return 200u32 + k;
            } else {
                i32 expected = code == 'i' ? int_value(k) : small_value(k);
                if ((i32)word != expected) return 100u32 + k;
            }
        }
    }
    return 0u32;
}

// One named integer: 21 integer and 8 floating registers remain.
[[runtime_only, noinline,
  variadic(GP *gp "gp_arg_area", f64 *fp "fp_arg_area",
           void *overflow "overflow_arg_area")]]
global u32 cross_one(in const u8 *format, ...) {
    return cross_walk(format, gp, 21u32, fp, 8u32, overflow);
}

// One integer and one floating register remain, so later unnamed arguments
// go to the stack; under cross32 an i64 may straddle s7 and the stack.
[[runtime_only, noinline,
  variadic(GP *gp "gp_arg_area", f64 *fp "fp_arg_area",
           void *overflow "overflow_arg_area")]]
global u32 cross_edge(in const u8 *format,
                      in u32 x1, in u32 x2, in u32 x3, in u32 x4, in u32 x5,
                      in u32 x6, in u32 x7, in u32 x8, in u32 x9, in u32 x10,
                      in u32 x11, in u32 x12, in u32 x13, in u32 x14,
                      in u32 x15, in u32 x16, in u32 x17, in u32 x18,
                      in u32 x19, in u32 x20,
                      in f64 y1, in f64 y2, in f64 y3, in f64 y4, in f64 y5,
                      in f64 y6, in f64 y7, ...) {
    if (x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + x13 +
            x14 + x15 + x16 + x17 + x18 + x19 + x20 != 210u32 ||
        y1 + y2 + y3 + y4 + y5 + y6 + y7 != 28.0) {
        return 400u32;
    }
    return cross_walk(format, gp, 1u32, fp, 1u32, overflow);
}

// The last named integer is on the stack; overflow_arg_area follows it.
[[runtime_only, noinline,
  variadic(GP *gp "gp_arg_area", f64 *fp "fp_arg_area",
           void *overflow "overflow_arg_area")]]
global u32 cross_stacked(in const u8 *format,
                         in u32 x1, in u32 x2, in u32 x3, in u32 x4,
                         in u32 x5, in u32 x6, in u32 x7, in u32 x8,
                         in u32 x9, in u32 x10, in u32 x11, in u32 x12,
                         in u32 x13, in u32 x14, in u32 x15, in u32 x16,
                         in u32 x17, in u32 x18, in u32 x19, in u32 x20,
                         in u32 x21, in u32 x22, ...) {
    if (x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + x13 +
            x14 + x15 + x16 + x17 + x18 + x19 + x20 + x21 != 231u32 ||
        x22 != 22u32) {
        return 500u32;
    }
    return cross_walk(format, gp, 0u32, fp, 8u32, overflow);
}

// The ninth named f64 is on the stack, read whole on 32-bit CPUs, and
// overflow_arg_area follows it.
[[runtime_only, noinline,
  variadic(GP *gp "gp_arg_area", f64 *fp "fp_arg_area",
           void *overflow "overflow_arg_area")]]
global u32 cross_float_stacked(in const u8 *format,
                               in f64 y1, in f64 y2, in f64 y3, in f64 y4,
                               in f64 y5, in f64 y6, in f64 y7, in f64 y8,
                               in f64 y9, ...) {
    if (y1 + y2 + y3 + y4 + y5 + y6 + y7 + y8 != 36.0 || y9 != -9.5) {
        return 600u32;
    }
    return cross_walk(format, gp, 21u32, fp, 0u32, overflow);
}

// A definition without bindings ignores its unnamed arguments.
[[runtime_only, noinline]]
global u32 cross_ignore(in const u8 *format, ...) {
    return format[0];
}

typedef u32 (*variadic_walker)(in const u8 *format, ...);

[[runtime_only, noinline]]
global variadic_walker cross_pick(in u32 which) {
    return which != 0u32 ? cross_one : cross_ignore;
}

// Each check is one line: a macro invocation cannot span lines.
#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status
#define EDGE 1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32, 9u32, 10u32, 11u32, 12u32, 13u32, 14u32, 15u32, 16u32, 17u32, 18u32, 19u32, 20u32, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0
#define FLOATS 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, -9.5
#define STACKED 1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32, 9u32, 10u32, 11u32, 12u32, 13u32, 14u32, 15u32, 16u32, 17u32, 18u32, 19u32, 20u32, 21u32, 22u32

// Returns 0, or 1000 * site + the failing code.
global u32 variadic_entry() {
    i32 w[9];
    i32 b[9];
    i64 l[9];
    f64 d[9];
    for (u32 k = 1u32; k < 9u32; k += 1u32) {
        w[k] = int_value(k);
        b[k] = small_value(k);
        l[k] = long_value(k);
        d[k] = double_value(k);
    }
    u32 status = 0u32;
    // Zero to eight unnamed arguments in registers.
    EXPECT(1u32, cross_one(""));
    EXPECT(2u32, cross_one("i", w[1]));
    EXPECT(3u32, cross_one("iiii", w[1], w[2], w[3], w[4]));
    EXPECT(4u32, cross_one("iiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]));
    EXPECT(5u32, cross_one("dddddddd", d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]));
    EXPECT(6u32, cross_one("idldilid", w[1], d[2], l[3], d[4], w[5], l[6], w[7], d[8]));
    EXPECT(7u32, cross_one("llll", l[1], l[2], l[3], l[4]));
    // f32 promotes to f64; 8- and 16-bit integers promote to i32.
    EXPECT(8u32, cross_one("dbb", (f32)d[1], (i8)b[2], (i16)b[3]));
    // Register positions run out.
    EXPECT(9u32, cross_edge("", EDGE));
    EXPECT(10u32, cross_edge("iii", EDGE, w[1], w[2], w[3]));
    EXPECT(11u32, cross_edge("ddd", EDGE, d[1], d[2], d[3]));
    EXPECT(12u32, cross_edge("lil", EDGE, l[1], w[2], l[3]));
    EXPECT(13u32, cross_edge("dlidldi", EDGE, d[1], l[2], w[3], d[4], l[5], d[6], w[7]));
    EXPECT(14u32, cross_edge("idldbdli", EDGE, w[1], d[2], l[3], d[4], (i16)b[5], d[6], l[7], w[8]));
    EXPECT(15u32, cross_stacked("", STACKED));
    EXPECT(16u32, cross_stacked("ildi", STACKED, w[1], l[2], d[3], w[4]));
    EXPECT(21u32, cross_float_stacked("did", FLOATS, d[1], w[2], d[3]));
    // Constants travel the same way.
    EXPECT(17u32, cross_one("idl", -6777i32, 2.5, 25769766740i64));
    if (cross_ignore("idl", w[1], d[2], l[3]) != 'i') return 18000u32;
    // Calls through a variadic function pointer.
    EXPECT(19u32, cross_pick(1u32)("ild", w[1], l[2], d[3]));
    if (cross_pick(0u32)("b", (i8)b[1]) != 'b') return 20000u32;
    return 0u32;
}
