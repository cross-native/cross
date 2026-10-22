// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// C forms a C port relies on; c_forms_driver.cpp runs the same operations in
// C++ and compares the results.

i32 record(const u8 *format, ...);

struct Sample {
    i32 value;
    u8 byte;
    i16 small;
    u16 wide;
    bool flag;
    f32 ratio;
    i64 big;
};

// Reading a qualified object yields an unqualified value, which a variadic
// call promotes as any other.
global i32 qualified_variadic(const struct Sample *s, const u8 *bytes,
                              const i16 negative, bool choose) {
    const f32 local = 2.5f32;
    volatile u8 port = 200u8;
    i32 count = record("i", s->value);
    count += record("i", bytes[1]);
    count += record("i", s->value + 0);
    count += record("i", +s->value);
    count += record("i", choose ? s->value : 0);
    count += record("i", s->byte);
    count += record("i", s->small);
    count += record("i", s->wide);
    count += record("i", s->flag);
    count += record("i", negative);
    count += record("d", s->ratio);
    count += record("d", local);
    count += record("l", s->big);
    count += record("i", port);
    return count;
}

// A negative or malformed number is an error, as in the C original.
static i32 parse(const u8 *s, out i64 v) {
    i64 sign = 1;
    v = 0;
    if (*s == '-') {
        sign = -1;
        ++s;
    }
    if (*s < '0' || *s > '9') return 1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (i64)(*s - '0');
        ++s;
    }
    v *= sign;
    return *s != 0;
}

// `v` is assigned whenever `parse` ran, so `v < 0` and the final return need
// no extra initialization.
global i32 check(const u8 *s, out i64 v) {
    if (!s || parse(s, v) || v < 0) {
        v = 0;
        return -1;
    }
    return 0;
}

static i32 measure(inout i32 calls, in i32 size, out i32 width, out i32 height) {
    ++calls;
    width = size;
    height = size * 2;
    return size + 1;
}

// `_` discards a result; the callee still runs.
global i32 discard_height(in i32 size) {
    i32 calls = 0;
    i32 width;
    const i32 result = measure(calls, size, width, _);
    return result * 100 + width + calls * 1000;
}

static i32 discard_at_translation(in i32 size) {
    i32 calls = 0;
    i32 height;
    return measure(calls, size, _, height) + height + calls;
}
$::static_assert(discard_at_translation(3) == 11, "translation-time discard");

global iptr qualified_difference(const u8 *end, u8 *begin) {
    return end - begin;
}

static iptr local_difference() {
    u8 buffer[8];
    const u8 *end = &buffer[6];
    return end - buffer;
}
$::static_assert(local_difference() == 6, "difference across qualification");

global f64 leading_dot(in f64 scale) {
    return .5 * scale + .25e1 + (f64).125f32;
}
$::static_assert(.5 == 0.5 && .5f32 * 2.0f32 == 1.0f32, "leading-dot literal");

static i32 bump(inout i32 value) {
    value += 1;
    return value;
}

static void nothing() {}

// `(void)` evaluates its operand and discards the value.
global i32 voided(in i32 x) {
    struct Sample sample = {x};
    (void)x;
    (void)sample;
    (void)nothing();
    (void)bump(x);
    (void)(x ? bump(x) : 0);
    return x;
}

static i32 voided_at_translation(in i32 x) {
    (void)bump(x);
    return x;
}
$::static_assert(voided_at_translation(4) == 5, "translation-time void cast");
