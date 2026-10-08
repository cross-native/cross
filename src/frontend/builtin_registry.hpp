// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

namespace cross {

// Compiler-owned expression operations. Types, preprocessing macros/queries,
// and target-owned instructions have separate registries. Keep source lookup,
// capability queries and driver inspection on the same operation-name table.
enum class CoreBuiltinKind { Intrinsic, Constant };

struct CoreBuiltinEntry {
    std::string_view name;
    CoreBuiltinKind kind;
    std::string_view description;
};

inline std::span<const CoreBuiltinEntry> core_expression_builtins() {
    using K = CoreBuiltinKind;
    static constexpr CoreBuiltinEntry entries[] = {
        {"$::expect", K::Intrinsic, "intrinsic"},
        {"$::assume", K::Intrinsic, "intrinsic"},
        {"$::unreachable", K::Intrinsic, "intrinsic"},
        {"$::trap", K::Intrinsic, "intrinsic"},
        {"$::alignof", K::Intrinsic, "intrinsic"},
        {"$::static_assert", K::Intrinsic, "intrinsic"},
        {"$::patch", K::Intrinsic, "code-generation intrinsic"},
        {"$::eval", K::Intrinsic, "translation-time requirement"},
        {"$::runtime", K::Intrinsic, "staged-evaluation barrier"},
        {"$::quote", K::Intrinsic, "procedural quotation"},
        {"$::unquote", K::Intrinsic, "procedural interpolation"},
        {"$::embed", K::Intrinsic, "translation intrinsic"},
        {"$::meta::len", K::Intrinsic, "translation intrinsic"},
        {"$::meta::at", K::Intrinsic, "translation intrinsic"},
        {"$::meta::slice", K::Intrinsic, "translation intrinsic"},
        {"$::meta::concat", K::Intrinsic, "translation intrinsic"},
        {"$::meta::data", K::Intrinsic, "translation intrinsic"},
        {"$::meta::alloc", K::Intrinsic, "translation intrinsic"},
        {"$::meta::cap", K::Intrinsic, "translation intrinsic"},
        {"$::meta::freeze", K::Intrinsic, "translation intrinsic"},
        {"$::meta::parse", K::Intrinsic, "translation intrinsic"},
        {"$::meta::call_site", K::Intrinsic, "identifier-context intrinsic"},
        {"$::meta::gensym", K::Intrinsic, "fresh-identifier intrinsic"},
        {"$::meta::tokens", K::Intrinsic, "syntax-tree token projection"},
        {"$::meta::child_count", K::Intrinsic, "translation intrinsic"},
        {"$::meta::child", K::Intrinsic, "translation intrinsic"},
        {"$::meta::replace_child", K::Intrinsic, "translation intrinsic"},
        {"$::meta::is_kind", K::Intrinsic, "translation intrinsic"},
        {"$::meta::is_production", K::Intrinsic, "translation intrinsic"},
        {"$::meta::is_extension", K::Intrinsic, "translation intrinsic"},
        {"$::meta::extension_match", K::Intrinsic, "translation intrinsic"},
        {"$::meta::spelling", K::Intrinsic, "translation intrinsic"},
        {"$::meta::token", K::Intrinsic, "translation intrinsic"},
        {"$::meta::group", K::Intrinsic, "translation intrinsic"},
        {"$::meta::children", K::Intrinsic, "translation intrinsic"},
        {"$::meta::delimiter", K::Intrinsic, "translation intrinsic"},
        {"$::meta::span", K::Intrinsic, "translation intrinsic"},
        {"$::meta::node_span", K::Intrinsic, "translation intrinsic"},
        {"$::meta::error", K::Intrinsic, "translation diagnostic"},
        {"$::meta::warning", K::Intrinsic, "translation diagnostic"},
        {"$::meta::note", K::Intrinsic, "translation diagnostic"},
        {"$::syntax::input", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::capture", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::node", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::count", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::at", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::is_variant", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::context", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::span", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::capture_span", K::Intrinsic, "translation intrinsic"},
        {"$::syntax::error", K::Intrinsic, "translation diagnostic"},
        {"$::syntax::warning", K::Intrinsic, "translation diagnostic"},
        {"$::syntax::note", K::Intrinsic, "translation diagnostic"},
        {"$::atomic_load", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_store", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_exchange", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_compare_exchange", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_fetch_add", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_fetch_sub", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_fetch_and", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_fetch_xor", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_fetch_or", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_thread_fence", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_signal_fence", K::Intrinsic, "atomic intrinsic"},
        {"$::atomic_is_lock_free", K::Intrinsic, "atomic query"},
        {"$::memory::relaxed", K::Constant, "atomic-order constant"},
        {"$::memory::acquire", K::Constant, "atomic-order constant"},
        {"$::memory::release", K::Constant, "atomic-order constant"},
        {"$::memory::acq_rel", K::Constant, "atomic-order constant"},
        {"$::memory::seq_cst", K::Constant, "atomic-order constant"},
    };
    return entries;
}

inline const CoreBuiltinEntry* find_core_expression_builtin(std::string_view name) {
    for (const auto& entry : core_expression_builtins())
        if (entry.name == name) return &entry;
    return nullptr;
}

inline bool is_machine_builtin_name(std::string_view name) {
    if (!name.starts_with("$::")) return false;
    const auto component = name.rfind("::") + 2;
    return component < name.size() && name[component] == '_';
}

} // namespace cross
