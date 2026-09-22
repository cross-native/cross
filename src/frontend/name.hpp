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
