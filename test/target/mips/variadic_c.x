// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Variadic interoperation with GCC C (variadic_c.c) under the compilation's C
// ABI, n64 or eabi32: Cross calls a C variadic function, forwards its unnamed
// arguments to a C function that takes a va_list, and C calls the Cross
// definitions below, which walk their unnamed arguments themselves. GCC's
// va_list is arg_area, except that with -DRECORD=1 (eabi32 with a
// double-precision FPU) it is a record that a definition fills from five
// states. Floating arguments are passed only with -DDOUBLES=1. The unnamed
// argument k of each format (1-based) has a known value.

[[runtime_only, noinline]]
global i32 int_value(in u32 k) {
    return 1000i32 * (i32)k - 7777i32;
}

[[runtime_only, noinline]]
global i64 long_value(in u32 k) {
    return ((i64)k << 33) - (i64)k * 12345i64 - 1i64;
}

[[runtime_only, noinline]]
global i32 small_value(in u32 k) {
    return 3i32 - 7i32 * (i32)k;
}

#if DOUBLES
[[runtime_only, noinline]]
global f64 double_value(in u32 k) {
    return (f64)k + 0.5;
}
#endif

global u32 c_check(in const u8 *format, ...);
global u32 c_calls_cross();

#if RECORD
// GCC's EABI va_list for an FPU with doubles.
struct va_record {
    void *overflow;
    void *gpr_top;
    void *fpr_top;
    u8 gpr_offset;
    u8 fpr_offset;
    u8 reserved[2];
};

global u32 c_vcheck(in const u8 *format, in struct va_record arguments);

// Reads the unnamed arguments as GCC's va_arg does: integers from the
// general-register area below gpr_top while gpr_offset is not 0, doubles from
// the floating area below fpr_top while fpr_offset is not 0, then the stack.
[[runtime_only, noinline]]
global u32 walk(in const u8 *format, in struct va_record list) {
    uptr overflow = (uptr)list.overflow;
    u32 gpr = list.gpr_offset;
    u32 fpr = list.fpr_offset;
    u32 k = 0u32;
    while (format[k] != 0u8) {
        u32 code = format[k];
        k += 1u32;
        uptr address = 0uptr;
        if (code == 'd') {
            if (fpr != 0u32) {
                address = (uptr)list.fpr_top - (uptr)fpr;
                fpr -= 8u32;
            } else {
                overflow = (overflow + 7uptr) & ~7uptr;
                address = overflow;
                overflow += 8uptr;
            }
            if (*(f64 *)address != double_value(k)) return 300u32 + k;
        } else if (code == 'l') {
            gpr &= ~7u32;
            if (gpr != 0u32) {
                address = (uptr)list.gpr_top - (uptr)gpr;
                gpr -= 8u32;
            } else {
                overflow = (overflow + 7uptr) & ~7uptr;
                address = overflow;
                overflow += 8uptr;
            }
            if (*(i64 *)address != long_value(k)) return 200u32 + k;
        } else {
            if (gpr != 0u32) {
                address = (uptr)list.gpr_top - (uptr)gpr;
                gpr -= 4u32;
            } else {
                address = overflow;
                overflow += 4uptr;
            }
            i32 expected = code == 'i' ? int_value(k) : small_value(k);
            if (*(i32 *)address != expected) return 100u32 + k;
        }
    }
    return 0u32;
}

#define LIST_STATES variadic(void *overflow "overflow_arg_area", void *gpr_top "gpr_top", void *fpr_top "fpr_top", u8 gpr_offset "gpr_offset", u8 fpr_offset "fpr_offset")
#define LIST_ARGUMENT make_list(overflow, gpr_top, fpr_top, gpr_offset, fpr_offset)

[[runtime_only, noinline]]
global struct va_record make_list(in void *overflow, in void *gpr_top,
                                  in void *fpr_top, in u8 gpr_offset,
                                  in u8 fpr_offset) {
    struct va_record list;
    list.overflow = overflow;
    list.gpr_top = gpr_top;
    list.fpr_top = fpr_top;
    list.gpr_offset = gpr_offset;
    list.fpr_offset = fpr_offset;
    list.reserved[0] = 0u8;
    list.reserved[1] = 0u8;
    return list;
}
#else
global u32 c_vcheck(in const u8 *format, in void *arguments);

// Walks GCC's va_list: one sequence of argument slots (eight bytes under
// n64, four under eabi32, where i64 and f64 values start at a multiple of
// eight).
[[runtime_only, noinline]]
global u32 walk(in const u8 *format, in void *arguments) {
    const uptr slot = sizeof(uptr);
    uptr cursor = (uptr)arguments;
    u32 k = 0u32;
    while (format[k] != 0u8) {
        u32 code = format[k];
        k += 1u32;
        if (code == 'i' || code == 'b') {
            i32 expected = code == 'i' ? int_value(k) : small_value(k);
            // n64 sign-extends a word to its eight-byte slot.
            i64 value = slot == 8uptr ? *(i64 *)cursor : (i64)*(i32 *)cursor;
            if (value != (i64)expected) return 100u32 + k;
            cursor += slot;
        } else {
            cursor = (cursor + 7uptr) & ~7uptr;
            if (code == 'l') {
                if (*(i64 *)cursor != long_value(k)) return 200u32 + k;
            } else {
#if DOUBLES
                if (*(f64 *)cursor != double_value(k)) return 300u32 + k;
#else
                return 900u32 + k;
#endif
            }
            cursor += 8uptr;
        }
    }
    return 0u32;
}

#define LIST_STATES variadic(void *arguments "arg_area")
#define LIST_ARGUMENT arguments
#endif

// One named pointer: the first unnamed argument takes the second register.
[[runtime_only, noinline, LIST_STATES]]
global u32 cross_walk(in const u8 *format, ...) {
    return walk(format, LIST_ARGUMENT);
}

[[runtime_only, noinline, LIST_STATES]]
global u32 cross_forward(in const u8 *format, ...) {
    return c_vcheck(format, LIST_ARGUMENT);
}

// Two named values: under eabi32 an unnamed i64 then starts at an even
// register.
[[runtime_only, noinline, LIST_STATES]]
global u32 cross_even(in const u8 *format, in i32 extra, ...) {
    if (extra != -5i32) return 400u32;
    return walk(format, LIST_ARGUMENT);
}

// Every argument register holds a named value, and the last named i64 is on
// the stack, so every unnamed argument follows it there.
[[runtime_only, noinline, LIST_STATES]]
global u32 cross_stack(in const u8 *format, in i64 a, in i64 b, in i64 c,
                       in i64 d, in i64 e, in i64 f, in i64 g, in i64 h,
                       ...) {
    if (a + b + c + d + e + f + g != 28i64 || h != -8i64) return 500u32;
    u32 status = walk(format, LIST_ARGUMENT);
    if (status != 0u32) return status;
    return c_vcheck(format, LIST_ARGUMENT);
}

// A frame that realigns for an over-aligned local and also allocates on the
// stack reaches its home area and stack arguments through its anchor.
[[runtime_only, noinline, LIST_STATES]]
global u32 cross_realigned(in const u8 *format, in u32 count, ...) {
    [[aligned(64)]] u8 line[64];
    u8 bytes[count];
    line[1] = 3u8;
    bytes[count - 1u32] = 4u8;
    if (((uptr)line & 63uptr) != 0uptr || line[1] + bytes[count - 1u32] != 7u32) {
        return 700u32;
    }
    return walk(format, LIST_ARGUMENT);
}

#if DOUBLES
// A named f64 travels in a floating register.
[[runtime_only, noinline, LIST_STATES]]
global u32 cross_after_double(in f64 first, in const u8 *format, ...) {
    if (first != 2.25) return 600u32;
    u32 status = walk(format, LIST_ARGUMENT);
    if (status != 0u32) return status;
    return c_vcheck(format, LIST_ARGUMENT);
}
#endif

// Each check is one line: a macro invocation cannot span lines.
#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status
#define STACKED 1i64, 2i64, 3i64, 4i64, 5i64, 6i64, 7i64, -8i64

// Returns 0, or 1000 * site + the failing code.
global u32 variadic_entry() {
    i32 w[11];
    i32 b[11];
    i64 l[11];
#if DOUBLES
    f64 d[11];
#endif
    for (u32 k = 1u32; k < 11u32; k += 1u32) {
        w[k] = int_value(k);
        b[k] = small_value(k);
        l[k] = long_value(k);
#if DOUBLES
        d[k] = double_value(k);
#endif
    }
    u32 status = 0u32;
    EXPECT(1u32, c_check(""));
    EXPECT(2u32, c_check("iiiiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10]));
    EXPECT(3u32, c_check("ilblilil", w[1], l[2], (i8)b[3], l[4], w[5], l[6], w[7], l[8]));
    EXPECT(4u32, cross_walk(""));
    EXPECT(5u32, cross_walk("iiiiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10]));
    EXPECT(6u32, cross_walk("ilblilil", w[1], l[2], (i16)b[3], l[4], w[5], l[6], w[7], l[8]));
    EXPECT(7u32, cross_forward("iiiiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10]));
    EXPECT(8u32, cross_forward("ilblilil", w[1], l[2], (i8)b[3], l[4], w[5], l[6], w[7], l[8]));
    EXPECT(9u32, cross_even("lilil", -5i32, l[1], w[2], l[3], w[4], l[5]));
    EXPECT(10u32, cross_stack("ilil", STACKED, w[1], l[2], w[3], l[4]));
    EXPECT(20u32, cross_realigned("ilil", 37u32, w[1], l[2], w[3], l[4]));
#if DOUBLES
    EXPECT(11u32, c_check("idldili", w[1], d[2], l[3], d[4], w[5], l[6], w[7]));
    EXPECT(12u32, c_check("dddddddddd", d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], (f32)d[10]));
    EXPECT(13u32, cross_walk("idldilid", w[1], d[2], l[3], d[4], w[5], l[6], w[7], d[8]));
    EXPECT(14u32, cross_walk("dddddddddd", d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], (f32)d[10]));
    EXPECT(15u32, cross_forward("idldilid", w[1], d[2], l[3], d[4], w[5], l[6], w[7], d[8]));
    EXPECT(16u32, cross_forward("dddddddddd", d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], (f32)d[10]));
    EXPECT(17u32, cross_after_double(2.25, "didld", d[1], w[2], d[3], l[4], d[5]));
    EXPECT(18u32, cross_stack("dlid", STACKED, d[1], l[2], w[3], d[4]));
#endif
    EXPECT(19u32, c_calls_cross());
    return 0u32;
}
