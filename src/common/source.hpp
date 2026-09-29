// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// Private name-resolution provenance, not a public meta-tree property. Unknown
// means the token has not been interpreted as a value name. Nonlocal records
// the absence of a visible local, so relocating a parsed use cannot capture a
// subsequently introduced local. Local names retain the declaring token's
// identity and syntax mark independently of the use token's own identity.
struct ValueBinding {
    enum class Kind { Unknown, Local, Nonlocal, Enumerator } kind{Kind::Unknown};
    TokenIdentity declaration;
    ExpansionId mark;
    // Local enumerators belong to a particular placement of their enum, not
    // just the declaring token (which is shared by copied public subtrees).
    std::shared_ptr<const NominalTypeIdentity> enumeration{};
    bool operator==(const ValueBinding&) const = default;
};

struct SyntaxContext {
    enum class Kind { CallSite, DefinitionSite } kind{Kind::CallSite};
    ExpansionId expansion;
    SourceLocation definition;
    SourceLocation invocation;
    std::string name_space;
    std::vector<std::string> imports;
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
    SourceLocation span;
    TokenIdentity identity;
    std::shared_ptr<const SyntaxContext> context;
    std::shared_ptr<const EmbedIdentity> embed;
    unsigned embed_piece{};
    ValueBinding value_binding;
    std::shared_ptr<const FreshIdentifier> fresh;
    // Opaque frontend tag-name provenance, retained by token projection.
    std::shared_ptr<const TagBinding> tag_binding;
    std::shared_ptr<const AliasBinding> alias_binding;

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
