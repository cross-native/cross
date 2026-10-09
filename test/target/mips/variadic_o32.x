// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// o32 variadic calls and definitions, compiled with -mabi=o32. A definition
// binds "arg_area", the C va_start address: the argument slots just past the
// named parameters, where a0-a3 are homed so that register and stack
// arguments form one sequence of words with 8-byte-aligned doubles and i64.
// The unnamed argument k of each format (1-based) has a known value.

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

// Walks the slots from a va_start address; returns 0 or a code naming the
// first wrong argument.
[[runtime_only, noinline]]
global u32 o32_walk(in const u8 *format, in void *arguments) {
    uptr cursor = (uptr)arguments;
    u32 k = 0u32;
    while (format[k] != 0u8) {
        u32 code = format[k];
        k += 1u32;
        if (code == 'i' || code == 'b') {
            i32 expected = code == 'i' ? int_value(k) : small_value(k);
            if (*(i32 *)cursor != expected) return 100u32 + k;
            cursor += 4uptr;
        } else {
            cursor = (cursor + 7uptr) & ~7uptr;
            if (code == 'l') {
                if (*(i64 *)cursor != long_value(k)) return 200u32 + k;
            } else if (*(f64 *)cursor != double_value(k)) {
                return 300u32 + k;
            }
            cursor += 8uptr;
        }
    }
    return 0u32;
}

// One named word: the first unnamed argument starts at odd slot 1.
[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_odd(in const u8 *format, ...) {
    return o32_walk(format, arguments);
}

// Two named words: the first unnamed argument starts at even slot 2.
[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_even(in const u8 *format, in i32 extra, ...) {
    if (extra != -5i32) return 400u32;
    return o32_walk(format, arguments);
}

// A leading f64 travels in f12 and still owns slots 0 and 1.
[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_after_double(in f64 first, in const u8 *format, ...) {
    if (first != 2.25) return 500u32;
    return o32_walk(format, arguments);
}

// The named i64 takes slots 4 and 5, so every unnamed argument is on the
// stack.
[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_stack(in const u8 *format, in i32 a, in i32 b, in i32 c,
                     in i64 d, ...) {
    if (a != 1i32 || b != 2i32 || c != 3i32 || d != -4i64) return 600u32;
    return o32_walk(format, arguments);
}

// A definition without bindings ignores its unnamed arguments.
[[runtime_only, noinline]]
global u32 o32_ignore(in const u8 *format, ...) {
    return format[0];
}

typedef u32 (*variadic_walker)(in const u8 *format, ...);

[[runtime_only, noinline]]
global variadic_walker o32_pick(in u32 which) {
    return which != 0u32 ? o32_odd : o32_ignore;
}

// Each check is one line: a macro invocation cannot span lines.
#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status

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
    // Zero to eight unnamed words; from the fourth on they are on the stack.
    EXPECT(1u32, o32_odd(""));
    EXPECT(2u32, o32_odd("i", w[1]));
    EXPECT(3u32, o32_odd("ii", w[1], w[2]));
    EXPECT(4u32, o32_odd("iii", w[1], w[2], w[3]));
    EXPECT(5u32, o32_odd("iiii", w[1], w[2], w[3], w[4]));
    EXPECT(6u32, o32_odd("iiiii", w[1], w[2], w[3], w[4], w[5]));
    EXPECT(7u32, o32_odd("iiiiii", w[1], w[2], w[3], w[4], w[5], w[6]));
    EXPECT(8u32, o32_odd("iiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7]));
    EXPECT(9u32, o32_odd("iiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]));
    // After one word, f64 and i64 skip odd slot 1 and start at slot 2; after
    // two words they move to the stack.
    EXPECT(10u32, o32_odd("d", d[1]));
    EXPECT(11u32, o32_odd("id", w[1], d[2]));
    EXPECT(12u32, o32_odd("iid", w[1], w[2], d[3]));
    EXPECT(13u32, o32_odd("di", d[1], w[2]));
    EXPECT(14u32, o32_odd("l", l[1]));
    EXPECT(15u32, o32_odd("il", w[1], l[2]));
    EXPECT(16u32, o32_odd("iil", w[1], w[2], l[3]));
    EXPECT(17u32, o32_odd("dldl", d[1], l[2], d[3], l[4]));
    EXPECT(18u32, o32_odd("idldili", w[1], d[2], l[3], d[4], w[5], l[6], w[7]));
    // f32 promotes to f64; 8- and 16-bit integers promote to i32.
    EXPECT(19u32, o32_odd("db", (f32)d[1], (i16)b[2]));
    EXPECT(20u32, o32_odd("bbd", (i8)b[1], (i16)b[2], (f32)d[3]));
    // An even first slot needs no padding.
    EXPECT(21u32, o32_even("d", -5i32, d[1]));
    EXPECT(22u32, o32_even("l", -5i32, l[1]));
    EXPECT(23u32, o32_even("idi", -5i32, w[1], d[2], w[3]));
    EXPECT(24u32, o32_after_double(2.25, "dii", d[1], w[2], w[3]));
    EXPECT(25u32, o32_after_double(2.25, "iid", w[1], w[2], d[3]));
    EXPECT(26u32, o32_stack("", 1i32, 2i32, 3i32, -4i64));
    EXPECT(27u32, o32_stack("idl", 1i32, 2i32, 3i32, -4i64, w[1], d[2], l[3]));
    // Constants travel the same way.
    EXPECT(28u32, o32_odd("idl", -6777i32, 2.5, 25769766740i64));
    if (o32_ignore("idlii", w[1], d[2], l[3], w[4], w[5]) != 'i') return 29000u32;
    // Calls through a variadic function pointer.
    EXPECT(30u32, o32_pick(1u32)("ild", w[1], l[2], d[3]));
    if (o32_pick(0u32)("b", (i8)b[1]) != 'b') return 31000u32;
    return 0u32;
}
