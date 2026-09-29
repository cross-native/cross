// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <functional>

namespace cross {

// Private nominal identity for a lexical/anonymous tag. Copies of a type keep
// this immutable object; its printable spelling is not its semantic identity.
// The source coordinates and occurrence ordinal are only a serialization key
// for model-driven mangling, never an internal name-lookup interchange format.
struct NominalTypeIdentity {
    TokenIdentity declaration;
    std::string source_unit;
    std::uint64_t ordinal{};
};

struct NominalTypeKey {
    std::string name;
    std::shared_ptr<const NominalTypeIdentity> identity{};

    [[nodiscard]] bool empty() const { return !identity && name.empty(); }

    bool operator==(const NominalTypeKey& other) const {
        if (identity || other.identity) return identity == other.identity;
        return name == other.name;
    }

    [[nodiscard]] std::string canonical_name() const {
        if (!identity) return name;
        // Delimit/length-prefix fields: paths and source spellings are not
        // restricted to identifier characters. No pointer value is emitted.
        const auto& id = *identity;
        return "<local:" + std::to_string(id.source_unit.size()) + ":" +
            id.source_unit + ":" + std::to_string(id.declaration.offset) + ":" +
            std::to_string(id.declaration.expansion.value) + ":" +
            std::to_string(id.declaration.output_position) + ":" +
            std::to_string(id.ordinal) + ">";
    }
};

struct NominalTypeKeyHash {
    std::size_t operator()(const NominalTypeKey& key) const {
        return key.identity ? std::hash<const NominalTypeIdentity*>{}(key.identity.get())
                            : std::hash<std::string>{}(key.name);
    }
};

} // namespace cross
