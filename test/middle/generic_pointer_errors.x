// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 values[4] = { 1u32, 2u32, 3u32, 4u32 };
global const u32 immutable = 7u32;
struct immutable_record { u32 member; u32 array[2]; };
global const struct immutable_record immutable_object = { 3u32, { 5u32, 7u32 } };
global u32 *cell;
[[thread_local]] global u32 tls = 9u32;

[[generic(u32 *P)]] static u32 *identity() { return P; }
[[generic(const u32 **P)]] static const u32 **nested() { return P; }
[[eval_only]] static u32 only_eval(in u32 x) { return x; }
typedef u32 (*Callback)(in u32 x);
[[generic(Callback F)]] static u32 call(in u32 x) { return F(x); }

[[runtime_only]] static u32 *runtime_address() { return values; }
static u32 *read_static() { return values + values[0]; }
static u32 *escape_local() { u32 local = 1u32; return &local; }
static u32 *uninitialized_pointer() { u32 *p; return p; }
static u32 *bad_cast(in const u32 *p) { return (u32 *)p; }
static u32 *indirect_pointer(in Callback only_eval) { return values + only_eval(0u32); }

global u32 *pointer_error(in u32 index) {
#if defined(LOCAL)
    u32 values[4];
    return identity::<values>();
#elif defined(PARAMETER)
    return identity::<values + index>();
#elif defined(QUALIFIERS)
    return identity::<&immutable>();
#elif defined(MEMBER_QUALIFIERS)
    return identity::<&immutable_object.member>();
#elif defined(ARRAY_QUALIFIERS)
    return identity::<&immutable_object.array[1]>();
#elif defined(NESTED)
    nested::<&cell>();
    return values;
#elif defined(CAST_QUALIFIERS)
    return identity::<(u32 *)(const u32 *)0uptr>();
#elif defined(WRONG_TYPE)
    return identity::<(i32 *)0uptr>();
#elif defined(UNKNOWN)
    return identity::<(1 ? values : missing)>();
#elif defined(NONZERO)
    return identity::<13>();
#elif defined(TLS)
    return identity::<&tls>();
#elif defined(PAST)
    return identity::<values + 5>();
#elif defined(BEFORE)
    return identity::<values - 1>();
#elif defined(OVERFLOW)
    return identity::<values + 0x4000000000000000u64>();
#elif defined(WIDTH)
    return identity::<(u32 *)0x100000000u64>();
#elif defined(ABSOLUTE_OVERFLOW)
    return identity::<(u32 *)0xffffffffffffffffu64 + 1>();
#elif defined(ABSOLUTE_UNDERFLOW)
    return identity::<(u32 *)1uptr - 1>();
#elif defined(VOID_ARITHMETIC)
    return identity::<(u32 *)((void *)1uptr + 1)>();
#elif defined(RUNTIME_CALL)
    return identity::<runtime_address()>();
#elif defined(STATIC_READ)
    return identity::<read_static()>();
#elif defined(ESCAPE)
    return identity::<escape_local()>();
#elif defined(UNINITIALIZED)
    return identity::<uninitialized_pointer()>();
#elif defined(INDIRECT_CALL)
    return identity::<indirect_pointer((Callback)0uptr)>();
#elif defined(CALL_QUALIFIERS)
    return identity::<bad_cast(&immutable)>();
#elif defined(ROUNDTRIP_QUALIFIERS)
    return identity::<(u32 *)(const void *)&immutable>();
#elif defined(EVAL_ADDRESS)
    call::<only_eval>(1u32);
    return values;
#elif defined(STRING_QUALIFIERS)
    return identity::<"abc">();
#elif defined(RUNTIME)
    return identity::<values + $::runtime(1)>();
#else
    return identity::<values>();
#endif
}
