// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[eval_only]] static uptr layout_object_size<T>() { T object = {}; return sizeof(object); }
[[eval_only]] static uptr layout_copy_size<T>() {
    T first = {};
    uptr *leaf = (uptr *)&first;
    *leaf = 7uptr;
    T second = first;
    if (*((uptr *)&second) != 7uptr) return 0uptr;
    return sizeof(second);
}

[[eval_only]] static uptr layout_private_copy_size<T>() {
#define CROSS_LAYOUT_ROW(HEAD, TAIL, KIND) struct PrivateCopy##HEAD { struct PrivateCopy##TAIL child[1]; };
#include "syntax_layout_rows.inc"
#undef CROSS_LAYOUT_ROW
    struct PrivateCopy240 { T value; };
    struct PrivateCopy0 first = {};
    T *leaf = (T *)&first;
    *leaf = (T)7u32;
    struct PrivateCopy0 second = first;
    if (*((T *)&second) != (T)7u32) return 0uptr;
    return sizeof(second);
}

[[eval_only]] static uptr layout_private_local_shape<T>() {
    typedef T LocalShape0;
#define CROSS_LAYOUT_ROW(HEAD, TAIL, KIND) typedef LocalShape##HEAD LocalShape##TAIL[1];
#include "syntax_layout_rows.inc"
#undef CROSS_LAYOUT_ROW
    LocalShape240 value = {};
    // The record's required extent must prepare the exact earlier local type,
    // with this helper's lexical owner rather than its requesting caller.
    struct LocalShapeRecord { u8 bytes[sizeof(value)]; };
    struct LocalShapeRecord object = {};
    return sizeof(object);
}

#define CROSS_LAYOUT_PLAIN(HEAD, TAIL) struct Plain##HEAD [[aligned(sizeof(struct Plain##TAIL))]] { u8 value; };
#define CROSS_LAYOUT_OBJECT(HEAD, TAIL) struct Object##HEAD [[aligned(layout_object_size<struct Object##TAIL>())]] { u8 value; };
#define CROSS_LAYOUT_COPY(HEAD, TAIL) struct Copy##HEAD { struct Copy##TAIL child; };
#define CROSS_LAYOUT_MIXED0(HEAD, TAIL) struct Mixed##HEAD [[aligned(sizeof(struct Mixed##TAIL))]] { u8 value; };
#define CROSS_LAYOUT_MIXED1(HEAD, TAIL) struct Mixed##HEAD { u8 value [[aligned($::alignof(struct Mixed##TAIL))]]; };
#define CROSS_LAYOUT_MIXED2(HEAD, TAIL) struct Mixed##HEAD { u8 value[sizeof(struct Mixed##TAIL)]; };
#define CROSS_LAYOUT_MIXED3(HEAD, TAIL) struct Mixed##HEAD { u8 value : sizeof(struct Mixed##TAIL); };
#define CROSS_LAYOUT_MIXED4(HEAD, TAIL) struct Mixed##HEAD { u8 [[ext_vector_type(sizeof(struct Mixed##TAIL))]] value; };
#define CROSS_LAYOUT_MIXED5(HEAD, TAIL) struct Mixed##HEAD { struct Mixed##TAIL value; };
#define CROSS_LAYOUT_ROW(HEAD, TAIL, KIND) CROSS_LAYOUT_PLAIN(HEAD, TAIL) CROSS_LAYOUT_OBJECT(HEAD, TAIL) CROSS_LAYOUT_COPY(HEAD, TAIL) CROSS_LAYOUT_MIXED##KIND(HEAD, TAIL)
// Each row points forward; source declaration order cannot pre-resolve layout.
// Keep input construction separate from translation-time construction budgets.
#include "syntax_layout_rows.inc"
struct Plain240 { uptr value; };
struct Object240 { uptr value; };
struct Mixed240 { u8 value; };
struct Copy240 { uptr value; };

struct CopyTagRoot { struct Copy0 child; u32 bits : 3; };
[[eval_only]] static uptr layout_tag_value() {
    $::meta::buffer raw = $::meta::alloc(sizeof(struct CopyTagRoot));
    for (uptr index = 0uptr; index < $::meta::cap(raw); ++index) $::meta::data(raw)[index] = 0u8;
    struct CopyTagRoot *root = (struct CopyTagRoot *)$::meta::data(raw);
    uptr *leaf = (uptr *)&root->child;
    *leaf = 7uptr;
    // No nominal range has yet been established in the raw backing. The
    // bit-field write checks the typed scalar tag through every child record.
    root->bits = 7u32;
    return (uptr)root->bits + *leaf;
}

namespace DeepLayoutExpansion {
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        u32 result = (u32)sizeof(struct Plain0) + (u32)sizeof(struct Object0) + (u32)sizeof(struct Mixed0);
        $::static_assert(result == 2u32 * (u32)sizeof(uptr) + 1u32, "expansion layout query");
        $::static_assert(layout_copy_size<struct Copy0>() == sizeof(uptr), "deep expansion record copy");
        $::static_assert(layout_private_copy_size<uptr>() == sizeof(uptr), "deep private expansion copy");
        $::static_assert(layout_private_copy_size<u8>() == sizeof(u8), "distinct private expansion instance");
        $::static_assert(layout_tag_value() == 14uptr, "deep expansion scalar-tag walk");
        $::static_assert(layout_private_local_shape<u8>() == sizeof(u8), "deep expansion local type dependency");
        $::static_assert(layout_private_local_shape<uptr>() == sizeof(uptr), "model expansion local type dependency");
        return $::quote { (u32)sizeof(struct Plain0) + (u32)sizeof(struct Object0) + (u32)sizeof(struct Mixed0) };
    }
    syntax Layout : expression { prefix "deep_layout"; match body:paren; expand expand; }
    syntax Layout;
#ifdef CUSTOM_SYNTAX_ABI
    [[noinline, abi("stack_result_abi")]] static u32 stack_identity(in u32 value) { return value; }
    [[noinline, abi("memory_result_abi")]] static u32 memory_identity(in u32 value) { return value; }
#endif
    [[noinline]] static u32 run(in u32 value) {
        struct Plain0 object = { .value = (u8)value };
        u32 result = (u32)object.value + deep_layout();
#ifdef CUSTOM_SYNTAX_ABI
        return memory_identity(stack_identity(result));
#else
        return result;
#endif
    }
    $::static_assert($::eval(run(0u32)) == 2u32 * (u32)sizeof(uptr) + 1u32, "forward layout evaluation");
    $::static_assert($::eval(layout_copy_size<struct Copy0>()) == sizeof(uptr), "deep required record copy");
    $::static_assert($::eval(layout_private_copy_size<uptr>()) == sizeof(uptr), "deep private required copy");
    $::static_assert($::eval(layout_private_copy_size<u8>()) == sizeof(u8), "distinct private required instance");
    $::static_assert($::eval(layout_tag_value()) == 14uptr, "deep required scalar-tag walk");
    $::static_assert($::eval(layout_private_local_shape<u8>()) == sizeof(u8), "deep required local type dependency");
    $::static_assert($::eval(layout_private_local_shape<uptr>()) == sizeof(uptr), "model required local type dependency");
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if ($::runtime(DeepLayoutExpansion::run(7u32)) != 8u32 + 2u32 * (u32)sizeof(uptr)) return 0u32;
    return 61u32;
}
