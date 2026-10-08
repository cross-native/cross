// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef struct { u8 tag; uptr value; } Layer0;
#ifdef CUSTOM_SYNTAX_ABI
typedef uptr (*Callback)(in uptr input) [[abi("stack_result_abi")]];
#else
typedef uptr (*Callback)(in uptr input);
#endif
typedef struct { Callback function; } CallbackLayer0;
#define CROSS_OUT_ROW(HEAD, TAIL) typedef struct { Layer##HEAD child[1]; } Layer##TAIL; typedef struct { CallbackLayer##HEAD child[1]; } CallbackLayer##TAIL;
#include "syntax_out_rows.inc"
#undef CROSS_OUT_ROW

[[syntax_expander]] static $::meta::tokens retain_out(in $::meta::syntax_match input) {
    $::meta::syntax definition = $::syntax::node(input, "definition");
    $::meta::syntax reparsed = $::meta::parse("function_def", $::quote { $::unquote(definition) }, $::syntax::context(input));
    return $::quote { $::unquote(reparsed) };
}
syntax RetainOut : item { prefix "retain_out"; match definition:function_def; expand retain_out; }
syntax RetainOut;

[[noinline]] static uptr add_tag(in uptr input) { return input + 7uptr; }
retain_out [[noinline]] static void initialize_callback(out CallbackLayer40 value) {
    CROSS_OUT_LEAF(value).function = add_tag;
}

retain_out [[noinline]] static uptr initialize(out Layer40 value, in uptr input) {
    CROSS_OUT_LEAF(value).tag = 7u8;
    CROSS_OUT_LEAF(value).value = input;
    return CROSS_OUT_LEAF(value).value + (uptr)CROSS_OUT_LEAF(value).tag;
}
union DeepChoice { Layer40 selected; uptr alternate; };
retain_out [[noinline]] static uptr choose_nested(out union DeepChoice value, in uptr input) {
    CROSS_OUT_LEAF(value.selected).tag = 7u8;
    CROSS_OUT_LEAF(value.selected).value = input;
    return CROSS_OUT_LEAF(value.selected).value + (uptr)CROSS_OUT_LEAF(value.selected).tag;
}
retain_out [[noinline]] static uptr choose_scalar(out union DeepChoice value, in uptr input) {
    value.alternate = input;
    return value.alternate;
}
static uptr required() {
    Layer40 value;
    union DeepChoice nested, scalar;
    if (initialize(value, 54uptr) != 61uptr || choose_nested(nested, 54uptr) != 61uptr ||
        choose_scalar(scalar, 61uptr) != 61uptr) return 0uptr;
    if (CROSS_OUT_LEAF(nested.selected).value != 54uptr || scalar.alternate != 61uptr) return 0uptr;
    return CROSS_OUT_LEAF(value).value + (uptr)CROSS_OUT_LEAF(value).tag;
}
$::static_assert($::eval(required()) == 61uptr, "deep evaluated output cells and captured copy-out");

#ifdef CUSTOM_SYNTAX_ABI
retain_out [[noinline, abi("stack_result_abi")]] static uptr stack_initialize(out Layer40 value, in uptr input) {
    return initialize(value, input);
}
retain_out [[noinline, abi("memory_result_abi")]] static uptr memory_initialize(out Layer40 value, in uptr input) {
    return initialize(value, input);
}
#endif
static volatile uptr seed = 54uptr;
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    Layer40 value = {};
    CallbackLayer40 callback = {};
    initialize_callback(callback);
    if (CROSS_OUT_LEAF(callback).function(seed) != 61uptr) return 0u32;
    union DeepChoice nested = {}, scalar = {};
    if (initialize(value, seed) != 61uptr ||
        CROSS_OUT_LEAF(value).tag != 7u8 || CROSS_OUT_LEAF(value).value != 54uptr ||
        choose_nested(nested, seed) != 61uptr ||
        CROSS_OUT_LEAF(nested.selected).value != 54uptr ||
        choose_scalar(scalar, seed + 7uptr) != 61uptr || scalar.alternate != 61uptr) return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (stack_initialize(value, seed) != 61uptr || CROSS_OUT_LEAF(value).value != 54uptr ||
        memory_initialize(value, seed) != 61uptr || CROSS_OUT_LEAF(value).tag != 7u8) return 0u32;
#endif
    return 61u32;
}
#undef CROSS_OUT_LEAF
