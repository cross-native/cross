// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A generic redeclaration repeats the record, union, and enumeration
// definitions of its header; every declaration then denotes the definition's
// per-instance types.

struct Box { T value; } wrap<T>(in T x);

// A call between the declaration and the definition.
static u32 early(in u32 x) { return wrap(x).value + 1u32; }

struct Box { U value; } wrap<U>(in U x) {
    struct Box result;
    result.value = x;
    return result;
}

// The definition comes first, and the redeclaration renames the parameter.
union Bits { T whole; u8 bytes[sizeof(T)]; } split<T>(in T x) {
    union Bits result;
    result.whole = x;
    return result;
}
union Bits { V whole; u8 bytes[sizeof(V)]; } split<V>(in V x);

// Nested definitions, declared before and after the definition.
struct Outer { struct Inner { T a; T b; } inner; enum Mode { Off, On = 3 } mode; } nest<T>(in T x);
struct Outer { struct Inner { T a; T b; } inner; enum Mode { Off, On = 3 } mode; } nest<T>(in T x) {
    struct Outer result;
    result.inner.a = x;
    result.inner.b = x + (T)1;
    result.mode = On;
    return result;
}
struct Outer { struct Inner { T a; T b; } inner; enum Mode { Off, On = 3 } mode; } nest<T>(in T x);

// Enumerations, including a value that depends on the type parameter.
enum Level { Low = 1, High = 5 } classify<T>(in T x);
enum Level { Low = 1, High = 5 } classify<T>(in T x) { return x > (T)10 ? High : Low; }
enum Width { Bytes = sizeof(T) } width<T>(in T x);
enum Width { Bytes = sizeof(W) } width<W>(in W x) { return Bytes; }

// An anonymous record in a parameter type.
uptr measure<T>(in struct { T a; u8 tail[3]; } *value);
uptr measure<T>(in struct { T a; u8 tail[3]; } *value) { return sizeof(*value); }

// Extents that depend on a value parameter are compared for each instance.
struct Buf { T items[N]; } fill<T, uptr N>(in T x);
struct Buf { T items[M]; } fill<T, uptr M>(in T x) {
    struct Buf result;
    for (uptr i = 0uptr; i < M; ++i) result.items[i] = x;
    return result;
}

static u32 combined(in u32 x) {
    return early(x) + (u32)wrap((u16)x).value + (u32)split(x * 0x01010101u32).bytes[0] +
        nest(x).inner.b + (u32)nest((u8)x).mode + (u32)classify(x) + (u32)width((u64)x) +
        (u32)measure<u32>((void *)0uptr) + (u32)fill<u16, 3>((u16)x).items[2];
}

// Computed during compilation.
global u32 folded = combined(20u32);

global u32 generic_header_redeclaration_entry() {
    $::static_assert(sizeof(nest((u8)1)) == 8uptr, "nested header records");
    $::static_assert(sizeof(fill<u16, 3>((u16)0)) == 6uptr, "per-instance extent");
    // 21 + 20 + 20 + 21 + 3 + 5 + 8 + 8 + 20
    return folded == 126u32 && $::runtime(combined(20u32)) == folded &&
        (u32)classify((u8)3) == 1u32 && split(0x11223344u32).bytes[0] == (u8)0x44;
}
