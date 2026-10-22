// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] global i32 dot(in const i32 *a, in const i32 *b,
                                      in u32 len, in i32 total) {
    for (u32 j = 0; j < len; ++j) total = total + a[j] * b[j];
    return total;
}

[[noinline]] global i32 dot_signed(in const i32 *a, in const i32 *b,
                                      in i32 len, in i32 total) {
    for (i32 j = 0; j < len; ++j) total = total + a[j] * b[j];
    return total;
}

[[noinline]] global i32 dot_uptr(in const i32 *a, in const i32 *b,
                                      in uptr len, in i32 total) {
    for (uptr j = 0; j < len; ++j) total = total + a[j] * b[j];
    return total;
}

[[noinline]] global i32 dot_iptr(in const i32 *a, in const i32 *b,
                                      in iptr len, in i32 total) {
    for (iptr j = 0; j < len; ++j) total = total + a[j] * b[j];
    return total;
}

[[noinline]] global u32 reference(in const i32 *a, in const i32 *b,
                                  in u32 len, in u32 total) {
    while (len != 0u32) {
        --len;
        total = total + (u32)a[len] * (u32)b[len];
    }
    return total;
}

[[noinline]] global i32 test_entry() {
    i32 a[100];
    i32 b[100];
    for (u32 i = 0u32; i < 100u32; ++i) {
        a[i] = (i32)i * 71i32 - 3500i32;
        b[i] = 1900i32 - (i32)i * 53i32;
    }
    if (dot((const i32*)0, (const i32*)0, 0u32, 19i32) != 19i32 ||
        dot_uptr((const i32*)0, (const i32*)0, 0, 19i32) != 19i32) return 6i32;
    if (dot_signed((const i32*)0, (const i32*)0, -1i32, 19i32) != 19i32 ||
        dot_signed((const i32*)0, (const i32*)0, -2147483647i32 - 1i32, 19i32) != 19i32)
        return 1i32;
    for (u32 offset = 0u32; offset < 3u32; ++offset) {
        for (u32 len = 0u32; len <= 97u32; ++len) {
            const u32 expected = reference(a + offset, b + offset, len, 37u32);
            if ((u32)dot(a + offset, b + offset, len, 37i32) != expected) return 2i32;
            if ((u32)dot_signed(a + offset, b + offset, (i32)len, 37i32) != expected) return 3i32;
            if ((u32)dot_iptr(a + offset, b + offset, (iptr)len, 37i32) != expected) return 5i32;
            if ((u32)dot_uptr(a + offset, b + offset, (uptr)len, 37i32) != expected) return 4i32;
        }
    }
    return 0i32;
}
