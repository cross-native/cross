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
    ValueBinding value_binding;
    // True only when the incoming token already carried a resolved use;
    // a later header prebinding pass must not retarget copied source input.
    bool value_binding_inherited{};
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

// Unresolved classifier keys use spelling and a syntax mark. Resolved value
// keys additionally name the declaring source token. Copied uses retain this
// identity rather than searching a replacement's same-spelled lexical locals.
struct NameKey {
    std::string spelling;
    ExpansionId context;
    ValueBinding binding;
    std::shared_ptr<const FreshIdentifier> fresh;

    NameKey() = default;
    explicit NameKey(std::string_view name, SourceLocation location = {})
        : spelling(name) {
        const auto origin = token_origin(location);
        fresh = origin.fresh;
        if (origin.context && origin.context->kind == SyntaxContext::Kind::DefinitionSite)
            context = origin.context->expansion;
    }
    void bind(ValueBinding value) {
        binding = value;
        if (value.kind != ValueBinding::Kind::Unknown) context = value.mark;
    }
    bool operator==(const NameKey&) const = default;
};

struct NameKeyHash {
    std::size_t operator()(const NameKey& name) const {
        const auto spelling = std::hash<std::string>{}(name.spelling);
        const auto context = std::hash<std::uint64_t>{}(name.context.value);
        auto result = spelling ^ (context + (spelling << 6) + (spelling >> 2));
        const auto mix = [&](std::size_t value) {
            result ^= value + (result << 6) + (result >> 2);
        };
        mix(static_cast<std::size_t>(name.binding.kind));
        mix(std::hash<const SourceFile*>{}(name.binding.declaration.source_unit));
        mix(std::hash<std::size_t>{}(name.binding.declaration.offset));
        mix(std::hash<std::uint64_t>{}(name.binding.declaration.expansion.value));
        mix(std::hash<std::size_t>{}(name.binding.declaration.output_position));
        mix(std::hash<const FreshIdentifier*>{}(name.fresh.get()));
        return result;
    }
};

template <typename Value>
using NameMap = std::unordered_map<NameKey, Value, NameKeyHash>;
using NameSet = std::unordered_set<NameKey, NameKeyHash>;

template <typename Declaration>
NameKey name_key(const Declaration& declaration) {
    NameKey result(declaration.name, declaration.location);
    result.bind({ValueBinding::Kind::Local, token_origin(declaration.location).identity,
                 result.context});
    return result;
}

} // namespace cross
