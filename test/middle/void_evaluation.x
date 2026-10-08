// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static u32 observed;
static void empty() {}
[[noinline, runtime_only]] static void write(in u32 value) { observed += value; }
[[noinline, runtime_only]] static void produce(out u32 value) { value = 7u32; }
[[noinline]] static void forward(in u32 value) { return write(value); }
[[noinline]] static void choose(in bool condition) {
    condition ? write(1u32) : write(4u32);
    condition ? empty() : empty();
}
[[noinline]] static void staged() { return $::eval(empty()); }
typedef void (*Callback)();
[[noinline, runtime_only]] static u32 use(in Callback callback) {
    callback();
    return callback != 0 ? 1u32 : 0u32;
}
[[macro]] static $::meta::tokens generate(in $::meta::tokens input) {
    return $::quote { $::eval(empty()); };
}
#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    empty();
    $::eval(empty());
    generate!{}
    staged();
    choose(1);
    choose(0);
    forward(8u32);
    if (use(&empty) != 1u32 || observed != 13u32) return 1u32;
    // An out actual contributes effects, not an input value. Its void call
    // must execute once even though the parameter result is discarded.
    produce(write(16u32));
    if (observed != 29u32) return 2u32;
    return 61u32;
}
