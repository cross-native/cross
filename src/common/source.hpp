// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

struct SourceFile;
struct SyntaxParseEnvironment;

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
};

struct SourceTokenOrigin {
    std::size_t begin{};
    std::size_t end{};
    TokenOrigin origin;
};

TokenOrigin token_origin(SourceLocation location);

struct SourceExpansion {
    std::size_t begin{};
    std::size_t end{};
    std::string macro_name;
    SourceLocation invocation;
    SourceLocation definition;
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
