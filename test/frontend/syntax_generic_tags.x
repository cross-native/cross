// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "local_tag_generic.x"
#include "local_tag_splice.x"

[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Expression : expression { prefix "tag_expression"; match "(" body:expr ")"; expand copy; }
syntax Definition : item { prefix "tag_definition"; match body:function_def; expand copy; }
[[syntax_expander]] static $::meta::tokens header(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header"))
                     $::unquote($::syntax::capture(input, "body")) };
}
syntax Header : item { prefix "tag_header"; match header:function_header body:block; expand header; }
[[macro]] static $::meta::tokens fragment(in $::meta::tokens input) { return input; }
syntax Definition, Header;

tag_definition [[noinline]] static T copied_generic_local<T>(in T value) {
    struct Local { T value; } object = {value};
    struct Local copied = LocalTagGeneric::pass(object);
    return copied.value;
}
tag_header [[noinline]] static T header_generic_local<T>(in T value) {
    struct Local { T value; } object = {value};
    struct Local copied = LocalTagGeneric::pass(object);
    return copied.value;
}
tag_header [[noinline]] static T deferred_generic_local(in T value)
    [[generic(fragment!(T))]] {
    struct Local { T value; } object = {value};
    struct Local copied = LocalTagGeneric::pass(object);
    return copied.value;
}
[[noinline]] static T generic_copy<T, u32 N>(in T amount) {
    syntax LocalTagSplice::Copy, LocalTagSplice::Type, LocalTagSplice::Twice,
           LocalTagSplice::TextTwice, Expression;
    tag_copy struct Local [[aligned(8)]] { T value; u8 padding[N]; };
    enum LocalLayout [[underlying(uptr)]] { LocalBytes = sizeof(struct Local) };
    if ((uptr)LocalBytes < sizeof(T) + (uptr)N || $::alignof(struct Local) != 8uptr) return (T)0u32;
    struct Local first = {amount};
    tag_type second, struct TypeTag { T value; T grid[N]; };
    second.value = first.value;
    tag_copy union Union [[packed, aligned(8)]] { T value; u8 byte; };
    union Union variant;
    variant.value = amount;
    if (sizeof(variant) != 8uptr || variant.value != amount) return (T)0u32;
    u32 side_effect = 0u32;
    uptr expression_alignment = tag_expression($::alignof(++side_effect));
    if (side_effect != 0u32 || expression_alignment != $::alignof(u32)) return (T)0u32;
    uptr expression_size = tag_expression(sizeof(struct ExpressionTag { T value; }));
    struct ExpressionTag expression_object = {amount};
    if (expression_size != sizeof(T) || expression_object.value != amount) return (T)0u32;
    uptr anonymous_size = tag_expression(sizeof(enum [[underlying(u16)]] { ExpressionValue = 7u16 }));
    if (anonymous_size != 2uptr || (u16)LocalTagGeneric::pass(ExpressionValue) != 7u16) return (T)0u32;
    tag_copy enum [[underlying(u32)]] { AnonymousCount = N };
    if ((u32)LocalTagGeneric::pass(AnonymousCount) != N) return (T)0u32;
    tag_type anonymous_type, struct { T value; u8 data[N]; };
    anonymous_type.value = amount;
    if (anonymous_type.value != amount) return (T)0u32;
    tag_twice {
        struct Repeated [[aligned(8)]] { T value; struct Repeated *next; u8 padding[N]; } repeated;
        repeated.value = second.value;
        repeated.next = &repeated;
        if (!LocalTagGeneric::same(&repeated, repeated.next) || repeated.next->value != amount) return (T)0u32;
        enum RepeatedEnum [[underlying(u32)]] { A = N, B = A + (u32)sizeof(T) } item = B;
        if (LocalTagSplice::enum_pair(item, B) != 2u32 * (N + (u32)sizeof(T))) return (T)0u32;
        enum RepeatedLayout [[underlying(uptr)]] { Bytes = sizeof(struct Repeated) };
        if ((uptr)Bytes < sizeof(T) + sizeof(uptr) + (uptr)N ||
            $::alignof(struct Repeated) != 8uptr) return (T)0u32;
        struct { T value; } anonymous = {amount}, transported = LocalTagGeneric::pass(anonymous);
        if (transported.value != amount) return (T)0u32;
        enum [[underlying(u32)]] { Count = N };
        if ((u32)LocalTagGeneric::pass(Count) != N) return (T)0u32;
    }
    tag_text_twice {
        struct Text [[aligned(8)]] { T value; u8 padding[N]; } text = {amount};
        if (text.value != amount) return (T)0u32;
        enum TextEnum [[underlying(u32)]] { TextA = N } item = TextA;
        if (LocalTagSplice::enum_pair(item, TextA) != 2u32 * N) return (T)0u32;
        enum TextLayout [[underlying(uptr)]] { TextBytes = sizeof(struct Text) };
        if ((uptr)TextBytes < sizeof(T) + (uptr)N || $::alignof(struct Text) != 8uptr) return (T)0u32;
        typedef union { T value; u8 byte; } Anonymous;
        Anonymous anonymous;
        anonymous.value = amount;
        Anonymous transported = LocalTagGeneric::pass(anonymous);
        if (transported.value != amount) return (T)0u32;
        enum [[underlying(u32)]] { Count = N };
        if ((u32)LocalTagGeneric::pass(Count) != N) return (T)0u32;
    }
    return second.value;
}
#ifdef CUSTOM_SYNTAX_ABI
[[noinline, abi("stack_result_abi")]] static T stack_pass<T>(in T value) { return value; }
[[noinline, abi("memory_result_abi")]] static T memory_pass<T>(in T value) { return value; }
[[noinline]] static T custom_transport<T>(in T amount) {
    struct Local { u64 first; T second; u8 bytes[sizeof(T)]; } object = {(u64)amount, amount};
    object.bytes[sizeof(T) - 1uptr] = 7u8;
    struct Local copied = memory_pass(object);
    enum Scalar [[underlying(u32)]] { A = 9u32 } item = stack_pass(A);
    if (copied.first != (u64)amount || (u32)item != 9u32 ||
        copied.bytes[sizeof(T) - 1uptr] != 7u8) return (T)0u32;
    return stack_pass(copied.second);
}
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (LocalTagGeneric::run(65549u32) != 65549u32 ||
        copied_generic_local(65549u32) != 65549u32 || copied_generic_local(7u16) != 7u16 ||
        header_generic_local(65549u32) != 65549u32 || header_generic_local(7u16) != 7u16 ||
        deferred_generic_local(65549u32) != 65549u32 || deferred_generic_local(7u16) != 7u16 ||
        generic_copy<u32, 5u32>(65549u32) != 65549u32 || generic_copy<u16, 7u32>(9u16) != 9u16)
        return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (custom_transport(65549u32) != 65549u32 || custom_transport(7u16) != 7u16) return 0u32;
#endif
    return 61u32;
}
