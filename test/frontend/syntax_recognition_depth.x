// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The registration supplies ample logical work/storage, never a larger depth
// limit. All 120 inner owners are discarded after read-only parsed recognition.
#define OPEN_TEN nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(nested_capture(
#define CLOSE_TEN ))))))))))
#define OPEN_120 OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN OPEN_TEN
#define CLOSE_120 CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN CLOSE_TEN
#define REGION_OPEN_TEN syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) { syntax (Regional) {
#define REGION_CLOSE_TEN } } } } } } } } } }
#define REGION_OPEN_120 REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN REGION_OPEN_TEN
#define REGION_CLOSE_120 REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN REGION_CLOSE_TEN

typedef u8 Word;
namespace Recognition {
    typedef u16 CapturedWord;
    [[syntax_expander]] static $::meta::tokens never_execute(in $::meta::syntax_match input) {
        $::meta::error($::syntax::span(input), "discarded nested capture executed");
        return $::quote { 0u32 };
    }
    [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
        $::meta::syntax node = $::syntax::node(input, "value");
        if (!$::meta::is_kind(node, "core"))
            $::meta::error($::syntax::span(input), "nested capture lost its public root");
        return $::quote { 13u32 };
    }
    syntax Nested : expression { prefix "nested_capture"; match "(" value:expr ")"; expand never_execute; }
    syntax Inspect : expression { prefix "inspect_nested"; match "(" value:expr ")"; expand inspect; }
    syntax Nested;
    syntax Inspect;
    [[macro]] static $::meta::tokens parameters(in $::meta::tokens input) { return input; }
    typedef u8 HeaderT;
    [[noinline]] static HeaderT header_identity<parameters!(HeaderT)>(in HeaderT value)
        [[aligned(sizeof(HeaderT parameters!(*)))]] { return value; }
    [[syntax_expander]] static $::meta::tokens regional(in $::meta::syntax_match input) {
        return $::quote { 61u32 };
    }
    syntax Regional : expression { prefix "regional_value"; match body:paren; expand regional; }
    [[noinline]] static u32 regional_value() { return 7u32; }
    namespace Imports { typedef u16 Word; }
    [[noinline]] static u32 composed_names() {
        // A separator and component come from one fragment. If a fragment
        // instead completes a qualified macro name, dispatch restarts at its
        // first component rather than invoking the final component alone.
        if (sizeof(Imports parameters!(:: Word) parameters!(*)) != sizeof(uptr)) return 0u32;
        using Recognition parameters!(:: Imports);
        if (sizeof(Word) != sizeof(u16)) return 0u32;
        return Recognition parameters!(:: parameters !(13u32));
    }
    [[noinline]] static u32 cursor_fragments() {
        u32 value = 1u32;
        parameters!(value += 2u32; value += 3u32;)
        if parameters!((value != 6u32)) return 0u32;
        value parameters!(+= 7u32);
        u32 values[2u32] parameters!(= {value, 0u32});
        return parameters!(values[0u32]);
    }
    REGION_OPEN_120 REGION_OPEN_120
        using Recognition::Imports;
        [[noinline]] static Word region_run() {
            if (sizeof(Word) != sizeof(u16)) return 0u16;
            return (Word)regional_value();
        }
    REGION_CLOSE_120 REGION_CLOSE_120
    $::static_assert(sizeof(Word) == sizeof(u8), "syntax-region import leaked after exit");
#ifdef CUSTOM_SYNTAX_ABI
    [[noinline, abi("stack_result_abi")]] static u32 stack_identity(in u32 value) { return value; }
    [[noinline, abi("memory_result_abi")]] static u32 memory_identity(in u32 value) { return value; }
#endif
    [[noinline]] static u32 run(in u32 value) {
        if (header_identity(300u32) != 300u32) return 0u32;
        if (composed_names() != 13u32) return 0u32;
        if (cursor_fragments() != 13u32) return 0u32;
        if (region_run() != 61u16 || regional_value() != 7u32) return 0u32;
        u32 inspected = inspect_nested(OPEN_120 (CapturedWord)13u16 CLOSE_120) + value;
#ifdef CUSTOM_SYNTAX_ABI
        return memory_identity(stack_identity(inspected));
#else
        return inspected;
#endif
    }
    $::static_assert(run(48u32) == 61u32, "recognition is opaque at translation time");
}

#undef OPEN_TEN
#undef CLOSE_TEN
#undef OPEN_120
#undef CLOSE_120
#undef REGION_OPEN_TEN
#undef REGION_CLOSE_TEN
#undef REGION_OPEN_120
#undef REGION_CLOSE_120

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return $::runtime(Recognition::run(48u32));
}
