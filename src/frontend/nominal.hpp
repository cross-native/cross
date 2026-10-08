// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/name.hpp"

#include <functional>

namespace cross {

// An immutable ownership handle shared by a generic header/body and its owned tags.
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
    std::shared_ptr<const FunctionScopeIdentity> function_scope{};
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

// Public recognition may retain an invalid type for inspection or repair.
// These private obligations follow captured bindings until a use survives;
// they are diagnostic data, not part of type identity or the public schema.
struct CapturedTypeError {
    SourceLocation location;
    std::string message;
};
using CapturedTypeErrors = std::vector<CapturedTypeError>;

inline std::uint64_t captured_type_error_storage(const CapturedTypeErrors& errors) {
    std::uint64_t result = 0;
    for (const auto& error : errors) result += 64 + error.message.size();
    return result;
}

struct RecordDecl;
struct EnumDecl;
// Copies of a quoting helper's block-scope definitions that a quoted tag or
// typedef needs: the records its type reaches and the enumerations their
// layouts name. Code that names the bound type brings them to its program.
struct CarriedDefinitions {
    std::vector<std::shared_ptr<const RecordDecl>> records;
    std::vector<std::shared_ptr<const EnumDecl>> enumerations;
};

// A parsed tag token names an exact nominal type. Declaration tokens also
// identify binders that a copied subtree may reintroduce in a new placement;
// copying a use token into a new declaration must not rebind the original type.
struct TagBinding {
    enum class Kind { Structure, Union, Enumeration } kind;
    enum class Role { Use, Declaration } role;
    TagBinding() = default;
    TagBinding(const TagBinding&) = default;
    TagBinding(TagBinding&&) = default;
    TagBinding& operator=(const TagBinding&) = default;
    TagBinding& operator=(TagBinding&&) = default;
    TagBinding(Kind kind, Role role, std::string spelling, NominalTypeKey type,
               BuiltinType underlying,
               std::shared_ptr<const CapturedTypeErrors> errors = {},
               std::shared_ptr<const TagBinding> source = {},
               std::uint64_t storage = {})
        : kind(kind), role(role), spelling(std::move(spelling)), type(std::move(type)),
          underlying(underlying), errors(std::move(errors)),
          declaration_source(std::move(source)), source_storage(storage) {}
    ~TagBinding();
    std::string spelling;
    NominalTypeKey type;
    BuiltinType underlying{};
    std::shared_ptr<const CapturedTypeErrors> errors{};
    std::shared_ptr<const TagBinding> declaration_source{};
    std::uint64_t source_storage{};
    std::shared_ptr<const CarriedDefinitions> carried{};
private:
    friend class detail::OwnerRelease;
    mutable detail::OwnerReleaseLink teardown_;
};

inline std::uint64_t tag_binding_storage(const std::shared_ptr<const TagBinding>& binding) {
    if (!binding) return 0;
    return 120 + binding->source_storage + binding->spelling.size() + binding->type.name.size() +
        (binding->type.identity ? 128 + binding->type.identity->source_unit.size() +
                                      binding->type.identity->instance_key.size() : 0) +
        (binding->errors ? captured_type_error_storage(*binding->errors) : 0) +
        (binding->carried ? 16 : 0);
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

// Filled monotonically by one declaration parser. Retained bindings expose
// only the const view; owners are lexical identities, never source spellings.
// Keeping every declarator lets one sibling survive edits to another sibling.
struct SharedSpecifierOwners {
    std::vector<TokenIdentity> declarations;
};

struct AliasBinding {
    // Shared written specifiers retain their resolved outer alias, but a
    // declarator's own generic may shadow it when the declaration is replayed.
    // Ordinary parsed type uses remain immutable under that operation.
    enum class Role { Use, Declaration, SharedSpecifierUse } role;
    std::string spelling;
    AliasDefinitionPtr definition;
    std::shared_ptr<const SharedSpecifierOwners> shared_owners;
    std::shared_ptr<const CarriedDefinitions> carried;
};

inline std::uint64_t alias_binding_storage(const std::shared_ptr<const AliasBinding>& binding) {
    return binding ? 80 + binding->spelling.size() + binding->definition->storage() +
        (binding->shared_owners ? 32 + binding->shared_owners->declarations.size() * 40 : 0) +
        (binding->carried ? 16 : 0) : 0;
}

inline std::uint64_t value_binding_storage(const ValueBinding& binding,
    const std::shared_ptr<const std::string>& spelling) {
    return (spelling ? 32 + spelling->size() : 0) +
        (binding.declaration_source ? binding.declaration_source->storage : 0) +
        (binding.placement ? 80 + binding.placement->name.size() +
            binding.placement->generic_parameters.size() * 8 + binding.placement->source_storage : 0) +
        (binding.enumeration ? 128 + binding.enumeration->source_unit.size() +
            binding.enumeration->instance_key.size() : 0);
}

inline std::uint64_t origin_binding_storage(const TokenOrigin& origin) {
    return tag_binding_storage(origin.tag_binding) + alias_binding_storage(origin.alias_binding) +
           tag_binding_storage(origin.tag_declaration_source) +
           function_label_storage(origin.label_binding.scope) +
           value_binding_storage(origin.value_binding, origin.value_spelling) +
           (origin.declaration_source ? 96 + value_binding_storage(*origin.declaration_source, {}) : 0) +
           fragment_lookup_storage(origin.fragment_lookup) +
           (origin.deferred_parameter_region ? 80 : 0);
}

} // namespace cross
