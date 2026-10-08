// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross {

struct Expr;
struct FunctionDecl;
struct Statement;

// Record members have a record-local namespace. Ordinary members compare by
// spelling; a private member compares only by its opaque token identity, never
// a serialized spelling or an unrelated lexical value-binding context.
struct MemberName {
    std::string spelling;
    std::shared_ptr<const FreshIdentifier> fresh;
    bool operator==(const MemberName& other) const {
        return fresh || other.fresh ? fresh == other.fresh : spelling == other.spelling;
    }
};
struct MemberNameHash {
    std::size_t operator()(const MemberName& name) const {
        return name.fresh ? std::hash<const FreshIdentifier*>{}(name.fresh.get())
                          : std::hash<std::string>{}(name.spelling);
    }
};
MemberName member_name(const Expr& expression);

// A selected code address uses source entities until HIR assigns typed IDs.
// The ordinal is serialization-only; private labels compare by declaration.
struct LabelAddressConstant {
    const FunctionDecl* owner{};
    const Statement* definition{};
    std::string global_name;
    std::uint64_t ordinal{};
    bool operator==(const LabelAddressConstant& other) const {
        if (owner != other.owner) return false;
        if (!global_name.empty() || !other.global_name.empty())
            return global_name == other.global_name;
        return definition == other.definition;
    }
};

// Namespace lookup is a use-site property, independent of local hygiene marks.
// Exact contexts are for names already selected by a semantic rewrite.
struct NameLookupContext {
    enum class Kind { Relative, Exact, Local } kind{Kind::Relative};
    std::string name_space;
    std::vector<std::string> imports;
    ValueBinding value_binding;
    std::shared_ptr<const FragmentNamespaceLookup> fragment_lookup;
    // True only when the incoming token already carried a resolved use;
    // a later header prebinding pass must not retarget copied source input.
    bool value_binding_inherited{};
    // The last component can have different provenance from a qualified
    // name's owner (for example an unquoted label after a quoted function).
    SourceLocation last_component_location;
    LabelBinding label_binding;
    std::shared_ptr<const LabelAddressConstant> label_address;
};

struct NameUse {
    std::string_view spelling;
    SourceLocation location{};
    const NameLookupContext* context{};

    NameUse(std::string_view name) : spelling(name) {}
    NameUse(const std::string& name) : spelling(name) {}
    NameUse(const char* name) : spelling(name) {}
    NameUse(std::string_view name, SourceLocation at) : spelling(name), location(at) {}
    NameUse(const Expr& expression);
};

// Probe every value category at each candidate before moving outwards. A
// caller must not search all object scopes before searching function scopes.
inline std::vector<std::string> namespace_candidates(
    NameUse name, std::string_view fallback_namespace = {},
    const std::vector<std::string>& fallback_imports = {});

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
        if (value.kind == ValueBinding::Kind::Local && value.placement)
            spelling = value.placement->name;
    }
    [[nodiscard]] bool uses_spelling() const {
        // Only an exact local placement replaces name lookup. Classifier keys,
        // qualified paths and legacy/synthetic token-only bindings still need
        // spelling; a fresh first component alone cannot identify a full path.
        return binding.kind != ValueBinding::Kind::Local || !binding.placement;
    }
    bool operator==(const NameKey& other) const {
        return context == other.context && binding == other.binding && fresh == other.fresh &&
            (!uses_spelling() || spelling == other.spelling);
    }
};

struct ValueBindingHash {
    std::size_t operator()(const ValueBinding& binding) const {
        auto result = static_cast<std::size_t>(binding.kind);
        const auto mix = [&](std::size_t value) {
            result ^= value + (result << 6) + (result >> 2);
        };
        if (binding.placement) {
            mix(std::hash<const ValuePlacementIdentity*>{}(binding.placement.get()));
            return result;
        }
        mix(std::hash<const SourceFile*>{}(binding.declaration.source_unit));
        mix(std::hash<std::size_t>{}(binding.declaration.offset));
        mix(std::hash<std::uint64_t>{}(binding.declaration.expansion.value));
        mix(std::hash<std::size_t>{}(binding.declaration.output_position));
        mix(std::hash<std::uint64_t>{}(binding.mark.value));
        mix(std::hash<const NominalTypeIdentity*>{}(binding.enumeration.get()));
        return result;
    }
};

struct NameKeyHash {
    std::size_t operator()(const NameKey& name) const {
        const auto spelling = name.uses_spelling() ? std::hash<std::string>{}(name.spelling) : 0;
        const auto context = std::hash<std::uint64_t>{}(name.context.value);
        auto result = spelling ^ (context + (spelling << 6) + (spelling >> 2));
        const auto mix = [&](std::size_t value) {
            result ^= value + (result << 6) + (result >> 2);
        };
        mix(ValueBindingHash{}(name.binding));
        mix(std::hash<const FreshIdentifier*>{}(name.fresh.get()));
        return result;
    }
};

template <typename Value>
using NameMap = std::unordered_map<NameKey, Value, NameKeyHash>;
using NameSet = std::unordered_set<NameKey, NameKeyHash>;

enum class FragmentNameDomain { Ordinary, Tag, Namespace };

struct FragmentNamespaceName {
    TokenIdentity stream;
    std::string source_namespace;
    std::string destination;
    ValueBinding value;
};

struct FragmentNamespaceTable {
    std::string name;
    NameMap<std::vector<FragmentNamespaceName>> ordinary;
    NameMap<std::vector<FragmentNamespaceName>> tags;
    NameMap<std::vector<FragmentNamespaceName>> namespaces;
    std::uint64_t storage{128};
    std::uint64_t entries{};
};

// Only the parser that opened the namespace publishes its completed table.
// Capture probes retain this identity, but mutate only their own draft table.
struct FragmentNamespaceIdentity {
    [[nodiscard]] const std::shared_ptr<const FragmentNamespaceTable>& completed() const { return completed_; }
private:
    friend class Parser;
    mutable std::shared_ptr<const FragmentNamespaceTable> completed_;
};

struct FragmentNamespaceScope {
    std::shared_ptr<FragmentNamespaceTable> draft;
    std::shared_ptr<const FragmentNamespaceIdentity> identity;
};

struct FragmentNamespaceLookup {
    struct Scope {
        std::shared_ptr<const FragmentNamespaceTable> preceding;
        std::shared_ptr<const FragmentNamespaceIdentity> identity;
        TokenIdentity stream;
        ExpansionId mark;
        std::string source_namespace;
    };
    NameKey key;
    // Innermost first. The preceding view serves source-order evaluation;
    // completion supplies later declarations from this original namespace.
    std::vector<Scope> scopes;
};

inline std::optional<FragmentNamespaceName> fragment_namespace_name(
    const FragmentNamespaceLookup& lookup, FragmentNameDomain domain, std::string_view spelling = {}) {
    if (spelling.empty()) spelling = lookup.key.spelling;
    const auto matching = [&](const auto& table, const FragmentNamespaceLookup::Scope& scope,
                              std::string_view name) -> const FragmentNamespaceName* {
        auto key = lookup.key;
        key.spelling = name;
        key.context = scope.mark;
        const auto found = table.find(key);
        if (found == table.end()) return nullptr;
        for (const auto& entry : found->second)
            if (entry.stream.source_unit == scope.stream.source_unit &&
                entry.stream.expansion == scope.stream.expansion &&
                entry.source_namespace == scope.source_namespace) return &entry;
        return nullptr;
    };
    for (const auto& scope : lookup.scopes) {
        const FragmentNamespaceTable* tables[] = {scope.identity->completed().get(), scope.preceding.get()};
        for (unsigned at = 0; at != 2; ++at) {
            if (!tables[at] || (at && tables[0] == tables[1])) continue;
            const auto& table = *tables[at];
            const auto& names = domain == FragmentNameDomain::Tag ? table.tags
                : domain == FragmentNameDomain::Namespace ? table.namespaces : table.ordinary;
            if (const auto* entry = matching(names, scope, spelling)) return *entry;
            const auto separator = spelling.find("::");
            if (separator == std::string::npos) continue;
            const auto prefix = spelling.substr(0, separator);
            const auto* entry = matching(table.namespaces, scope, prefix);
            // Function owners participate in qualified label lookup too.
            if (!entry && domain == FragmentNameDomain::Ordinary) {
                entry = matching(table.ordinary, scope, prefix);
                if (entry && entry->value.kind != ValueBinding::Kind::Function) entry = nullptr;
            }
            if (entry) {
                auto result = *entry;
                result.destination += spelling.substr(separator);
                result.value = {};
                return result;
            }
        }
    }
    return {};
}

inline std::uint64_t fragment_lookup_storage(const std::shared_ptr<const FragmentNamespaceLookup>& lookup) {
    if (!lookup) return 0;
    std::uint64_t result = 104 + lookup->key.spelling.size();
    for (const auto& scope : lookup->scopes) {
        result += 96 + scope.source_namespace.size() + scope.preceding->storage + scope.preceding->name.size();
        if (scope.identity->completed() && scope.identity->completed() != scope.preceding)
            result += scope.identity->completed()->storage + scope.identity->completed()->name.size();
    }
    return result;
}

inline std::vector<std::string> namespace_candidates(
    NameUse name, std::string_view fallback_namespace,
    const std::vector<std::string>& fallback_imports) {
    std::vector<std::string> result;
    if (name.context &&
        (name.context->value_binding.kind == ValueBinding::Kind::Object ||
         name.context->value_binding.kind == ValueBinding::Kind::Function) &&
        name.context->value_binding.placement)
        return {name.context->value_binding.placement->name};
    if (name.context && (name.context->kind == NameLookupContext::Kind::Local ||
                         name.context->label_address)) return result;
    const auto lookup = name.context ? name.context->fragment_lookup : token_origin(name.location).fragment_lookup;
    // Label resolution derives an owner-prefix lookup from its complete name.
    // This is not a different spelling supplied by edited source tokens.
    if (lookup && (lookup->key.spelling == name.spelling ||
            (lookup->key.spelling.starts_with(name.spelling) &&
             std::string_view(lookup->key.spelling).substr(name.spelling.size()).starts_with("::"))))
        if (const auto selected = fragment_namespace_name(*lookup, FragmentNameDomain::Ordinary, name.spelling))
            return {selected->destination};
    const bool relative = name.context
        ? name.context->kind == NameLookupContext::Kind::Relative
        : name.spelling.find("::") == std::string_view::npos;
    if (relative) {
        auto prefix = std::string(name.context ? name.context->name_space : fallback_namespace);
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

// Source-local lookup uses the defining function, never a destination or a
// layout caller. A function's label namespace is completed once its own core
// body has been parsed. Nested captures can retain the handle before that
// completion, without changing their immutable public nodes or snapshots.
struct FunctionScopeIdentity {
    struct Labels {
        NameMap<TokenIdentity> declarations;
        std::uint64_t storage{32};
    };
    [[nodiscard]] const Labels* labels() const { return labels_.get(); }
private:
    friend class Parser;
    // Only the parser of this newly allocated function scope may publish it;
    // statement/fragment probes and copied destination scopes never do so.
    mutable std::shared_ptr<const Labels> labels_;
};

inline std::uint64_t function_label_storage(
    const std::shared_ptr<const FunctionScopeIdentity>& scope) {
    return scope && scope->labels() ? scope->labels()->storage : 0;
}

inline LabelBinding resolved_label_binding(LabelBinding binding, const NameKey& name) {
    if (binding.kind != LabelBinding::Kind::Unknown || !binding.scope ||
        !binding.scope->labels() || name.spelling.find("::") != std::string::npos)
        return binding;
    const auto& declarations = binding.scope->labels()->declarations;
    const auto found = declarations.find(name);
    if (found != declarations.end()) {
        binding.kind = LabelBinding::Kind::Reference;
        binding.declaration = found->second;
    }
    return binding;
}

inline LabelBinding resolved_label_binding(NameUse name) {
    const auto split = name.spelling.rfind("::");
    const auto component = split == std::string_view::npos
        ? name.spelling : name.spelling.substr(split + 2);
    const auto location = name.context && name.context->last_component_location.valid()
        ? name.context->last_component_location : name.location;
    return resolved_label_binding(name.context ? name.context->label_binding
        : token_origin(location).label_binding, NameKey(component, location));
}

template <typename Declaration>
NameKey name_key(const Declaration& declaration) {
    NameKey result(declaration.name, declaration.location);
    if constexpr (requires { declaration.binding; }) {
        if (declaration.binding.kind != ValueBinding::Kind::Unknown) {
            result.bind(declaration.binding);
            return result;
        }
    }
    result.bind({ValueBinding::Kind::Local, token_origin(declaration.location).identity,
                 result.context});
    return result;
}

} // namespace cross
