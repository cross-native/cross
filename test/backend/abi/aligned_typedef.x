// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Records whose layout typedef alignment changes are classified by that
// layout; a value of an aligned typedef itself travels as its base type.

typedef u32 wide_u32 [[aligned(16)]];
typedef u8 wide_u8 [[aligned(8)]];
typedef f64 wide_f64 [[aligned(16)]];
struct one { wide_u32 value; };
struct over { u32 a; wide_u32 b; };
struct small { wide_u8 a; u8 b; };
struct floating { wide_f64 x; };
struct array { wide_u32 x[2]; };
struct pair { u32 a; u32 b; };
typedef struct pair pair16 [[aligned(16)]];

[[abi("sysv_abi"), link_name("sysv_one"), noinline]]
global struct one sysv_one(in struct one value, in u32 k) {
    struct one result = value;
    result.value += k;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_over"), noinline]]
global struct over sysv_over(in struct over value) {
    struct over result = value;
    result.a += 1u32;
    result.b += 2u32;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_small"), noinline]]
global struct small sysv_small(in struct small value) {
    struct small result = value;
    result.a += 1u8;
    result.b += 2u8;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_floating"), noinline]]
global struct floating sysv_floating(in struct floating value, in f64 k) {
    struct floating result = value;
    result.x += k;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_array"), noinline]]
global struct array sysv_array(in struct array value) {
    struct array result = value;
    result.x[0] += 1u32;
    result.x[1] += 2u32;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_pair16"), noinline]]
global pair16 sysv_pair16(in pair16 value, in u32 k) {
    pair16 result = value;
    result.a += k;
    result.b += k;
    return result;
}

[[abi("sysv_abi"), link_name("sysv_scalars"), noinline]]
global wide_u32 sysv_scalars(in wide_u32 a, in u32 b, in wide_u32 c) {
    return a * 100u32 + b * 10u32 + c;
}

[[abi("ms_abi"), link_name("ms_one"), noinline]]
global struct one ms_one(in struct one value, in u32 k) {
    struct one result = value;
    result.value += k;
    return result;
}

[[abi("ms_abi"), link_name("ms_small"), noinline]]
global struct small ms_small(in struct small value) {
    struct small result = value;
    result.a += 1u8;
    result.b += 2u8;
    return result;
}

[[abi("ms_abi"), link_name("ms_pair16"), noinline]]
global pair16 ms_pair16(in pair16 value, in u32 k) {
    pair16 result = value;
    result.a += k;
    result.b += k;
    return result;
}

[[abi("ms_abi"), link_name("ms_scalars"), noinline]]
global wide_u32 ms_scalars(in wide_u32 a, in u32 b, in wide_u32 c) {
    return a * 100u32 + b * 10u32 + c;
}

[[noinline]] global struct over cross_over(in struct over value, in wide_u32 k) {
    struct over result = value;
    result.a += k;
    result.b += k;
    return result;
}

[[noinline]] static struct array cross_array(in struct array value) {
    struct array result = value;
    result.x[0] *= 2u32;
    result.x[1] *= 3u32;
    return result;
}

[[noinline]] global pair16 cross_pair16(in pair16 value) {
    pair16 result = value;
    result.a ^= result.b;
    return result;
}

// Cross ABI calls, with records and scalars of aligned typedefs.
[[link_name("cross_aligned_calls"), noinline]]
global i32 aligned_calls() {
    struct over o;
    o.a = 1u32;
    o.b = 2u32;
    const struct over r = cross_over(o, 10u32);
    struct array a;
    a.x[0] = 3u32;
    a.x[1] = 4u32;
    const struct array s = cross_array(a);
    pair16 p;
    p.a = 6u32;
    p.b = 5u32;
    const pair16 q = cross_pair16(p);
    return r.a == 11u32 && r.b == 12u32 && s.x[0] == 6u32 && s.x[1] == 12u32 &&
           q.a == 3u32 && q.b == 5u32 ? 1 : 0;
}
