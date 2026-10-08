// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/owner_release.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cross {

struct SourceFile;
struct SyntaxParseEnvironment;
struct SyntaxNode;
struct NominalTypeIdentity;
struct TagBinding;
struct AliasBinding;
struct FunctionScopeIdentity;
struct FragmentNamespaceLookup;

struct SourceLocation {
    const SourceFile* file{};
    std::size_t offset{};
    unsigned line{1};
    unsigned column{1};

    [[nodiscard]] bool valid() const { return file != nullptr; }
};

struct ExpansionId {
    std::uint64_t value{};
    bool operator==(const ExpansionId&) const = default;
};

// Opaque private name identity. Its visible token spelling is only a hint;
// copied tokens share this object for declaration/reference binding.
struct FreshIdentifier {
    ExpansionId expansion;
    std::uint64_t ordinal{};
    std::string prefix;
    std::string source_unit;
};

std::string fresh_identifier_name(const FreshIdentifier& identifier);
std::string fresh_identifier_link_stem(const FreshIdentifier& identifier);

// Lexical identity is independent of where a token is subsequently printed.
// Original tokens use their primary source and offset; constructed tokens use
// the expansion and output position. A null source marks an unplaced token.
struct TokenIdentity {
    const SourceFile* source_unit{};
    std::size_t offset{};
    ExpansionId expansion;
    std::size_t output_position{};
    bool operator==(const TokenIdentity&) const = default;
};

struct TokenIdentityHash {
    std::size_t operator()(const TokenIdentity& identity) const noexcept {
        auto hash = std::hash<const SourceFile*>{}(identity.source_unit);
        const auto mix = [&](std::size_t value) {
            hash ^= value + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        };
        mix(std::hash<std::size_t>{}(identity.offset));
        mix(std::hash<std::uint64_t>{}(identity.expansion.value));
        mix(std::hash<std::size_t>{}(identity.output_position));
        return hash;
    }
};

struct TokenRegion {
    TokenIdentity first;
    TokenIdentity end;
};

// Private name-resolution provenance, not a public meta-tree property. Unknown
// means the token has not been interpreted as a value name. Nonlocal records
// the absence of a visible local, so relocating a parsed use cannot capture a
// subsequently introduced local. Local names retain the declaring token's
// identity and syntax mark independently of the use token's own identity.
struct ValueBinding;
struct ValuePlacementIdentity {
    ValuePlacementIdentity() = default;
    ValuePlacementIdentity(const ValuePlacementIdentity&) = default;
    ValuePlacementIdentity(ValuePlacementIdentity&&) = default;
    ValuePlacementIdentity& operator=(const ValuePlacementIdentity&) = default;
    ValuePlacementIdentity& operator=(ValuePlacementIdentity&&) = default;
    ~ValuePlacementIdentity();
    enum class Kind { Object, Function } kind{Kind::Object};
    // A local spelling or namespace-qualified source symbol name (never a
    // target link name). Pointer identity distinguishes declaration placements.
    std::string name;
    // Grammar classification travels with an exact function binding, including
    // through a body splice that restores its original lookup environment.
    // These are source parameter kinds, never ABI transport information.
    enum class GenericParameterKind { Type, Value };
    std::vector<GenericParameterKind> generic_parameters;
    // Only a copied declaring token contributes ancestry. This lets a header
    // parsed independently of its body reconnect the body's earlier binding,
    // without treating sibling copies of the same token as one declaration.
    std::shared_ptr<const ValueBinding> source_binding;
    std::uint64_t source_storage{};
private:
    friend class detail::OwnerRelease;
    mutable detail::OwnerReleaseLink teardown_;
};

struct ValueDeclarationSource;

struct ValueBinding {
    enum class Kind { Unknown, Local, Nonlocal, Enumerator, Object, Function } kind{Kind::Unknown};
    ValueBinding() = default;
    ValueBinding(const ValueBinding&) = default;
    ValueBinding(ValueBinding&&) = default;
    ValueBinding& operator=(const ValueBinding&) = default;
    ValueBinding& operator=(ValueBinding&&) = default;
    ValueBinding(Kind kind, TokenIdentity declaration, ExpansionId mark,
                 std::shared_ptr<const NominalTypeIdentity> enumeration = {},
                 std::shared_ptr<const ValuePlacementIdentity> placement = {},
                 std::shared_ptr<const ValueDeclarationSource> source = {})
        : kind(kind), declaration(declaration), mark(mark),
          enumeration(std::move(enumeration)), placement(std::move(placement)),
          declaration_source(std::move(source)) {}
    ~ValueBinding();
    TokenIdentity declaration;
    ExpansionId mark;
    // Enumerators belong to a particular placement of their enum, not just
    // the declaring token (which is shared by copied public subtrees). This
    // owner is independent of whether lookup also exposes a namespace name.
    // A Nonlocal binding may retain a namespace-enumerator candidate here for
    // declaration-copy remapping; ordinary namespace lookup remains authoritative
    // until that declaration is actually reintroduced in a new placement.
    std::shared_ptr<const NominalTypeIdentity> enumeration{};
    std::shared_ptr<const ValuePlacementIdentity> placement{};
    // Enumerator declaration-copy ancestry, excluded from binding equality.
    std::shared_ptr<const ValueDeclarationSource> declaration_source{};
    bool operator==(const ValueBinding& other) const {
        if (kind != other.kind) return false;
        // Redeclarations retain their own declaring-token anchors but share
        // the source entity's placement. Local placements are always fresh.
        if (placement || other.placement) return placement == other.placement;
        return declaration == other.declaration && mark == other.mark && enumeration == other.enumeration;
    }
};

struct ValueDeclarationSource {
    ValueDeclarationSource() = default;
    ValueDeclarationSource(const ValueDeclarationSource&) = default;
    ValueDeclarationSource(ValueDeclarationSource&&) = default;
    ValueDeclarationSource& operator=(const ValueDeclarationSource&) = default;
    ValueDeclarationSource& operator=(ValueDeclarationSource&&) = default;
    ValueDeclarationSource(ValueBinding binding, std::uint64_t storage)
        : binding(std::move(binding)), storage(storage) {}
    ValueBinding binding;
    std::uint64_t storage{};
private:
    friend class detail::OwnerRelease;
    mutable detail::OwnerReleaseLink teardown_;
};

// Labels use a function-wide namespace independent of ordinary values. A
// parsed reference retains its source scope even before a forward declaration
// is known; completed parsed units additionally retain the declaring token.
struct LabelBinding {
    enum class Kind { Unknown, Definition, Reference } kind{Kind::Unknown};
    TokenIdentity declaration;
    std::shared_ptr<const FunctionScopeIdentity> scope;
    bool operator==(const LabelBinding&) const = default;
};

struct SyntaxContext {
    enum class Kind { CallSite, DefinitionSite } kind{Kind::CallSite};
    ExpansionId expansion;
    SourceLocation definition;
    SourceLocation invocation;
    std::string name_space;
    std::vector<std::string> imports;
    // Exact declarations already reflected in imports. A later provenance
    // refinement must not promote an old outer import above a saved inner one.
    std::vector<TokenIdentity> import_declarations;
    struct EntityId {
        std::uint32_t value{};
        bool operator==(const EntityId&) const = default;
    };
    struct Binding {
        enum class Family { Item, StatementExpression } family;
        std::string prefix;
        EntityId entity;
        bool operator==(const Binding&) const = default;
    };
    std::vector<Binding> syntax_bindings;
    // Opaque frontend snapshot. Common source/provenance code does not know
    // parser tables or source-language types.
    std::shared_ptr<const SyntaxParseEnvironment> parse_environment;
};

struct EmbedSnapshot {
    std::filesystem::path path;
    // Filled on the first mandatory evaluation, then shared by every
    // occurrence selecting this file during the compilation group.
    std::shared_ptr<const std::string> bytes;
};

// One pre-expansion embed expression owns its selected file identity. The
// four lexical pieces carry the same identity through token copying.
struct EmbedIdentity {
    std::string written_path;
    std::filesystem::path selected_path;
    SourceLocation logical_location;
    std::shared_ptr<EmbedSnapshot> snapshot;
};

struct TokenOrigin {
    enum class LookupMode { Lexical, Invocation };
    SourceLocation span;
    TokenIdentity identity;
    std::shared_ptr<const SyntaxContext> context;
    // call_site fixes this identifier's import lookup to the invocation.
    // Its unchanged token identity must not re-admit generated block imports.
    LookupMode lookup_mode{LookupMode::Lexical};
    std::shared_ptr<const EmbedIdentity> embed;
    unsigned embed_piece{};
    ValueBinding value_binding;
    // Explicit-context parsing resets identifier lookup, but a copied
    // declaring token still has declaration ancestry if used as a binder.
    // Expression lookup never reads this separate declaration-only channel.
    std::shared_ptr<const ValueBinding> declaration_source;
    // Complete parsed value-name spelling. Editing a qualified name must not
    // reuse the first component's old binding for a different complete name.
    std::shared_ptr<const std::string> value_spelling;
    // A first-parse use may await completion of its original generated
    // namespace. Projection preserves this lookup, never a destination scope.
    std::shared_ptr<const FragmentNamespaceLookup> fragment_lookup;
    std::shared_ptr<const FreshIdentifier> fresh;
    // Opaque frontend tag-name provenance, retained by token projection.
    std::shared_ptr<const TagBinding> tag_binding;
    // Explicit-context parsing resets uses, not copied tag-binder ancestry.
    std::shared_ptr<const TagBinding> tag_declaration_source;
    std::shared_ptr<const AliasBinding> alias_binding;
    LabelBinding label_binding;
    // A token projected from a parsed/deferred public unit retains negative
    // local lookup too. Raw primitive captures and fresh constructors remain
    // composable until interpreted in a source grammar position.
    bool value_context_captured{};
    // A wholly deferred declaration/header has not bound its parameters yet.
    // Only declarations from this original header region may complete them.
    std::shared_ptr<const TokenRegion> deferred_parameter_region;
    // Constructors may supply a whole retained span. An absent final anchor
    // denotes the same point as span; this metadata never participates in
    // lexical identity or name lookup.
    SourceLocation span_end;

    SourceLocation last_span() const { return span_end.valid() ? span_end : span; }

    TokenOrigin() = default;
    TokenOrigin(SourceLocation span, TokenIdentity identity,
                std::shared_ptr<const SyntaxContext> context,
                std::shared_ptr<const EmbedIdentity> embed, unsigned embed_piece,
                ValueBinding value_binding,
                std::shared_ptr<const FreshIdentifier> fresh = {},
                std::shared_ptr<const TagBinding> tag_binding = {},
                std::shared_ptr<const AliasBinding> alias_binding = {})
        : span(span), identity(identity), context(std::move(context)),
          embed(std::move(embed)), embed_piece(embed_piece),
          value_binding(value_binding), fresh(std::move(fresh)),
          tag_binding(std::move(tag_binding)), alias_binding(std::move(alias_binding)) {}
};

struct SourceTokenOrigin {
    std::size_t begin{};
    std::size_t end{};
    TokenOrigin origin;
    // A structured syntax fragment travels beside its serialized marker.
    // Its subtree is never reconstructed from the marker's spelling.
    std::shared_ptr<const SyntaxNode> splice;

    SourceTokenOrigin() = default;
    SourceTokenOrigin(std::size_t begin, std::size_t end, TokenOrigin origin,
                      std::shared_ptr<const SyntaxNode> splice = {})
        : begin(begin), end(end), origin(std::move(origin)), splice(std::move(splice)) {}
};

TokenOrigin token_origin(SourceLocation location);

struct SourceExpansion {
    enum class Kind { ProceduralMacro, SyntaxExtension, FragmentParse, StructuredSplice };
    std::size_t begin{};
    std::size_t end{};
    std::string macro_name;
    SourceLocation invocation;
    SourceLocation definition;
    Kind kind{Kind::ProceduralMacro};
    SourceLocation expander;

    SourceExpansion() = default;
    SourceExpansion(std::size_t begin, std::size_t end, std::string name,
                    SourceLocation invocation, SourceLocation definition,
                    Kind kind = Kind::ProceduralMacro,
                    SourceLocation expander = {})
        : begin(begin), end(end), macro_name(std::move(name)),
          invocation(invocation), definition(definition), kind(kind),
          expander(expander) {}
};

struct SourceFile {
    std::filesystem::path path;
    std::string text;
    std::vector<std::size_t> line_starts{0};
    std::vector<SourceExpansion> expansions;
    // One logical origin per emitted source line; empty for written source.
    std::vector<SourceLocation> line_origins;
    // Primary translation-unit identity is distinct from a line's include path.
    std::vector<std::string> line_units;
    // Sorted by begin; serialization records one exact range per token.
    std::vector<SourceTokenOrigin> token_origins;

    SourceFile(std::filesystem::path path, std::string text,
               std::vector<SourceExpansion> expansions = {},
               std::vector<SourceTokenOrigin> token_origins = {},
               std::vector<SourceLocation> line_origins = {},
               std::vector<std::string> line_units = {});
    [[nodiscard]] std::string_view line(unsigned line) const;
    [[nodiscard]] std::string source_unit_at(unsigned line) const;
    [[nodiscard]] const SourceExpansion* expansion_at(
        std::size_t offset) const;
    [[nodiscard]] const TokenOrigin* token_origin_at(std::size_t offset) const;
    [[nodiscard]] const SourceTokenOrigin* source_token_origin_at(std::size_t offset) const;
};

class SourceManager {
public:
    const SourceFile* load(const std::filesystem::path& path, std::string& error);
    const SourceFile* add(std::filesystem::path path, std::string text);
    const SourceFile* add(std::filesystem::path path, std::string text,
                          std::vector<SourceExpansion> expansions,
                          std::vector<SourceTokenOrigin> token_origins = {},
                          std::vector<SourceLocation> line_origins = {},
                          std::vector<std::string> line_units = {});
    ExpansionId next_expansion() { return {++next_expansion_}; }

private:
    std::vector<std::unique_ptr<SourceFile>> files_;
    std::uint64_t next_expansion_{};
};

bool write_file(const std::filesystem::path& path, std::string_view text,
                std::string& error);
std::string quote_command_arg(std::string_view value);

} // namespace cross
