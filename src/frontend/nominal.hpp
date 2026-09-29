// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <functional>

namespace cross {

// An immutable ownership handle shared by a generic body and its lexical tags.
// It is not a function name, source spelling, or runtime object.
struct GenericTagOwner {};

// Private nominal identity for a lexical/anonymous tag. Copies of a type keep
// this immutable object; its printable spelling is not its semantic identity.
// Source coordinates, occurrence ordinal and concrete instance are a serialization key
// for model-driven mangling, never an internal name-lookup interchange format.
struct NominalTypeIdentity {
    TokenIdentity declaration;
    std::string source_unit;
    std::uint64_t ordinal{};
    std::shared_ptr<const GenericTagOwner> generic_owner{};
    // Serialization-only identity of the enclosing concrete generic instance.
    std::string instance_key{};
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
            std::to_string(id.ordinal) +
            (id.instance_key.empty() ? std::string{} :
                ":instance:" + std::to_string(id.instance_key.size()) + ":" + id.instance_key) + ">";
    }
};

struct NominalTypeKeyHash {
    std::size_t operator()(const NominalTypeKey& key) const {
        return key.identity ? std::hash<const NominalTypeIdentity*>{}(key.identity.get())
                            : std::hash<std::string>{}(key.name);
    }
};

enum class BuiltinType;

// A parsed tag token names an exact nominal type. Declaration tokens also
// identify binders that a copied subtree may reintroduce in a new placement;
// copying a use token into a new declaration must not rebind the original type.
struct TagBinding {
    enum class Kind { Structure, Union, Enumeration } kind;
    enum class Role { Use, Declaration } role;
    std::string spelling;
    NominalTypeKey type;
    BuiltinType underlying{};
};

inline std::uint64_t tag_binding_storage(const std::shared_ptr<const TagBinding>& binding) {
    if (!binding) return 0;
    return 96 + binding->spelling.size() + binding->type.name.size() +
        (binding->type.identity ? 112 + binding->type.identity->source_unit.size() +
                                      binding->type.identity->instance_key.size() : 0);
}

struct Type;

// A typedef is transparent to type equality and ABI classification. This handle
// identifies only its source binding and owns an immutable, detached type graph;
// consumers always obtain a fresh graph, never the stored mutable Type objects.
class AliasDefinition {
public:
    AliasDefinition(const std::shared_ptr<Type>& type, std::uint64_t storage);
    [[nodiscard]] std::shared_ptr<Type> instantiate() const;
    [[nodiscard]] std::uint64_t storage() const { return storage_; }
private:
    std::shared_ptr<Type> type_;
    std::uint64_t storage_;
};
using AliasDefinitionPtr = std::shared_ptr<const AliasDefinition>;

struct AliasBinding {
    enum class Role { Use, Declaration } role;
    std::string spelling;
    AliasDefinitionPtr definition;
};

inline std::uint64_t alias_binding_storage(const std::shared_ptr<const AliasBinding>& binding) {
    return binding ? 64 + binding->spelling.size() + binding->definition->storage() : 0;
}

inline std::uint64_t origin_binding_storage(const TokenOrigin& origin) {
    return tag_binding_storage(origin.tag_binding) + alias_binding_storage(origin.alias_binding);
}

} // namespace cross
