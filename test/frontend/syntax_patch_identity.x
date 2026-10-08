// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global uptr patch_copy_sink;
global uptr patch_branch_sink;
global uptr patch_loop_sink;
global uptr patch_index_sinks[2][2];
global u32 patch_expected = 7u32;
static uptr patch_unevaluated_sink;
$::static_assert(sizeof($::patch(3u16, patch_unevaluated_sink)) == sizeof(u16), "patch source type");
$::static_assert($::alignof($::patch(3u16)) == $::alignof(u16), "patch source alignment");
$::static_assert(1u32 || $::patch(3u32, patch_unevaluated_sink), "unselected patch has no cell");

[[macro]] static $::meta::tokens patch_twice(in $::meta::tokens input) {
    if (sizeof($::patch(3u16)) != sizeof(u16)) return $::quote { 0u32 };
    return $::quote { ($::unquote(input)) + ($::unquote(input)) };
}
[[macro]] static $::meta::tokens patch_fresh(in $::meta::tokens input) {
    return $::quote { $::patch(5u32) };
}
[[macro]] static $::meta::tokens patch_constructed_twice(in $::meta::tokens input) {
    $::meta::tokens value = $::quote { $::patch(5u32) };
    return $::quote { $::unquote(value) + $::unquote(value) };
}
[[macro]] static $::meta::tokens patch_loop(in $::meta::tokens input) {
    return $::quote { {
        u32 total = 0u32;
        for (u32 i = 0u32; i < 2u32; ++i) total += $::unquote(input);
        total += $::unquote(input);
        return total;
    } };
}

[[syntax_expander]] static $::meta::tokens patch_choose(in $::meta::syntax_match input) {
    $::meta::syntax condition = $::syntax::node(input, "condition");
    $::meta::syntax value = $::syntax::node(input, "value");
    return $::quote { $::unquote(condition) ? $::unquote(value) : $::unquote(value) + 1u32 };
}
syntax PatchChoose : expression {
    prefix "patch_choose";
    match "(" condition:expr "," value:expr ")";
    expand patch_choose;
}
syntax PatchChoose;
[[syntax_expander]] static $::meta::tokens patch_project(in $::meta::syntax_match input) {
    $::meta::tokens value = $::meta::tokens($::syntax::node(input, "value"));
    return $::quote { patch_twice!($::unquote(value)) };
}
syntax PatchProject : expression {
    prefix "patch_project"; match "(" value:expr ")"; expand patch_project;
}
syntax PatchProject;

[[noinline]] static u32 copied_patch() {
    return patch_twice!($::patch(7u32, patch_copy_sink));
}
[[noinline]] static u32 branch_patch(in bool first) {
    return patch_choose(first, $::patch(7u32, patch_branch_sink));
}
[[noinline]] static u32 loop_patch() {
    patch_loop!($::patch(7u32, patch_loop_sink))
}
[[noinline]] static u32 projected_patch() {
    return patch_project($::patch(9u32));
}
[[generic(T), noinline]] static u32 generic_patch() {
    return patch_twice!($::patch((u32)sizeof(T)));
}
[[noinline]] static u32 fresh_patches() { return patch_fresh!() + patch_fresh!(); }
[[noinline]] static u32 constructed_patches() { return patch_constructed_twice!(); }
[[noinline]] static u32 byte_patch() { return patch_twice!($::patch(11u8)); }
[[noinline]] static u32 half_patch() { return patch_twice!($::patch(13u16)); }
[[noinline]] static u64 wide_patch() { return patch_twice!($::patch(0x123456789u64)); }

struct PatchIndexLayout { u8 head; uptr tail; };
static uptr patch_index<T>() {
    T object = {};
    return sizeof(object) / sizeof(T);
}
[[noinline]] static u32 indexed_patch() {
    return patch_twice!($::patch(7u32,
        patch_index_sinks[patch_index<struct PatchIndexLayout>()][$::alignof(uptr) / $::alignof(uptr)]));
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi("stack_result_abi"), noinline]] static u32 stacked_patch() {
    return patch_twice!($::patch(17u32));
}
struct patch_pair { u64 first; u64 second; };
[[abi("memory_result_abi"), noinline]] static struct patch_pair indirect_patch() {
    struct patch_pair result = {
        patch_twice!($::patch(19u64)), patch_twice!($::patch(23u64))
    };
    return result;
}
#endif

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifdef CUSTOM_SYNTAX_ABI
    struct patch_pair pair = indirect_patch();
    if (stacked_patch() != 34u32 || pair.first != 38u64 || pair.second != 46u64) return 0u32;
#endif
    // Visit both arms: neither copied use may rely on a value produced by
    // the other arm, regardless of their target-selected physical layout.
    if (branch_patch(0) != patch_expected + 1u32) return 1u32;
    if (branch_patch(1) != patch_expected) return 2u32;
    if (copied_patch() != patch_expected * 2u32) return 3u32;
    if (loop_patch() != patch_expected * 3u32) return 4u32;
    if (patch_copy_sink == 0uptr || patch_branch_sink == 0uptr || patch_loop_sink == 0uptr ||
        patch_copy_sink == patch_branch_sink || patch_copy_sink == patch_loop_sink ||
        patch_branch_sink == patch_loop_sink) return 5u32;
    if (projected_patch() != 18u32) return 6u32;
    if (generic_patch::<u32>() != 8u32) return 7u32;
    if (generic_patch::<u64>() != 16u32) return 8u32;
    if (fresh_patches() != 10u32) return 9u32;
    if (constructed_patches() != 10u32) return 10u32;
    if (byte_patch() != 22u32) return 11u32;
    if (half_patch() != 26u32) return 12u32;
    if (wide_patch() != 0x2468acf12u64) return 13u32;
    if (sizeof($::patch(3u16, patch_unevaluated_sink)) != sizeof(u16) ||
        patch_unevaluated_sink != 0uptr) return 14u32;
    if (indexed_patch() != patch_expected * 2u32 || !patch_index_sinks[1][1] ||
        patch_index_sinks[0][0] || patch_index_sinks[0][1] || patch_index_sinks[1][0] ||
        patch_index_sinks[1][1] == patch_copy_sink) return 15u32;
    return 61u32;
}
