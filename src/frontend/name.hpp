// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross {

struct Expr;

// Namespace lookup is a use-site property, independent of local hygiene marks.
// Exact contexts are for names already selected by a semantic rewrite.
struct NameLookupContext {
    enum class Kind { Relative, Exact, Local } kind{Kind::Relative};
    std::string name_space;
    std::vector<std::string> imports;
};

struct NameUse {
    std::string_view spelling;
    SourceLocation location{};
    const NameLookupContext* context{};

    NameUse(std::string_view name) : spelling(name) {}
    NameUse(const std::string& name) : spelling(name) {}
    NameUse(const char* name) : spelling(name) {}
    NameUse(const Expr& expression);
};

// Probe every value category at each candidate before moving outwards. A
// caller must not search all object scopes before searching function scopes.
inline std::vector<std::string> namespace_candidates(
    NameUse name, std::string_view fallback_namespace = {},
    const std::vector<std::string>& fallback_imports = {}) {
    std::vector<std::string> result;
    if (name.context && name.context->kind == NameLookupContext::Kind::Local) return result;
    const bool relative = name.context
        ? name.context->kind == NameLookupContext::Kind::Relative
        : name.spelling.find("::") == std::string_view::npos;
    if (relative) {
        auto prefix = std::string(name.context ? name.context->name_space
                                              : fallback_namespace);
        while (!prefix.empty()) {
            result.push_back(prefix + "::" + std::string(name.spelling));
            const auto separator = prefix.rfind("::");
            if (separator == std::string::npos) break;
            prefix.resize(separator);
        }
        for (const auto& imported : name.context ? name.context->imports : fallback_imports)
            result.push_back(imported + "::" + std::string(name.spelling));
    }
    result.emplace_back(name.spelling);
    return result;
}

// A local binding is identified by spelling and its introducing syntax mark,
// never by an encoded/mangled spelling. Copied source input uses the ordinary
// lexical scope; copied generated input retains the introducing expansion.
struct NameKey {
    std::string spelling;
    ExpansionId context;

    NameKey() = default;
    explicit NameKey(std::string_view name, SourceLocation location = {})
        : spelling(name) {
        const auto origin = token_origin(location);
        if (origin.context && origin.context->kind == SyntaxContext::Kind::DefinitionSite)
            context = origin.context->expansion;
    }
    bool operator==(const NameKey&) const = default;
};

struct NameKeyHash {
    std::size_t operator()(const NameKey& name) const {
        const auto spelling = std::hash<std::string>{}(name.spelling);
        const auto context = std::hash<std::uint64_t>{}(name.context.value);
        return spelling ^ (context + (spelling << 6) + (spelling >> 2));
    }
};

template <typename Value>
using NameMap = std::unordered_map<NameKey, Value, NameKeyHash>;
using NameSet = std::unordered_set<NameKey, NameKeyHash>;

template <typename Declaration>
NameKey name_key(const Declaration& declaration) {
    return NameKey(declaration.name, declaration.location);
}

} // namespace cross
