// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(VALUE_CONVERSION)
typedef u64 (*plain_fn)(in u64 a);
typedef u64 (*manual_fn)(in u64 a "rdi") -> "*r9"
    [[clobber("rbx"), stack_cleanup("callee")]];

// Only a named function can be adapted; a held value keeps its interface.
global manual_fn convert(in plain_fn callback) {
    return callback;
}
#elif defined(CONDITIONAL_ARMS) || defined(CONDITIONAL_VALUE)
typedef u64 (*plain_fn)(in u64 a, in u64 b);

u64 manual_source(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 100u64 + b;
}

u64 manual_other(in u64 a "rdi", in u64 b "rsi") -> "rax" {
    return a * 200u64 + b;
}

u64 plain_source(in u64 a, in u64 b) {
    return a + b * 2u64;
}

// A conditional chooses no adapter: its arms must share one interface, which
// becomes the interface of its value.
global plain_fn choose(in bool first) {
#if defined(CONDITIONAL_ARMS)
    return first ? manual_source : plain_source;
#else
    return first ? manual_source : manual_other;
#endif
}
#elif defined(VARIADIC_CALL)
typedef u64 (*variadic_fn)(in u64 a "rdi", ...);

global u64 call(in variadic_fn callback) {
    return callback(1u64, 2u64);
}
#elif defined(RESERVED_RESULT)
typedef u64 (*reserved_fn)(in u64 a "rdi") -> "*rsp";

global u64 call(in reserved_fn callback) {
    return callback(1u64);
}
#elif defined(CLEANUP_OUTPUT)
typedef void (*cleanup_fn)(inout u64 a "push=>pop") [[stack_cleanup("callee")]];

global u64 call(in cleanup_fn callback) {
    u64 value = 1u64;
    callback(value);
    return value;
}
#elif defined(STACK_POINTER_ENDPOINT)
[[naked]]
global void switch_stack(in uptr frame "rsp") {
    $::_ret();
}

global void call(in uptr frame) {
    switch_stack(frame);
}
#elif defined(MIPS_ENDPOINTS)
typedef u32 (*manual_fn)(in u32 a "a1") -> "v1";

global u32 call(in manual_fn callback) {
    return callback(1u32);
}
#elif defined(MIPS_ADAPTER)
typedef u32 (*manual_fn)(in u32 a "a1") -> "v1";

static u32 plain(in u32 a) {
    return a + 1u32;
}

global manual_fn adapt() {
    return plain;
}
#endif
