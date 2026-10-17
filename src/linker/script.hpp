// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/source.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cross::linker::script {

// The GNU ld linker-script subset the linker accepts, as a syntax tree.

struct Expression;
using ExpressionPtr = std::unique_ptr<Expression>;

struct Expression {
    enum class Kind : std::uint8_t { Number, Symbol, Dot, Unary, Binary, Conditional, Function };
    Kind kind{};
    std::uint64_t number{};
    // A symbol, a function, or the section or region a function names.
    std::string name;
    std::string op;
    std::vector<ExpressionPtr> operands;
    SourceLocation location;
};

struct Assignment {
    // "." for the location counter.
    std::string symbol;
    // "=", "+=", "-=", "*=", "/=", "<<=", ">>=", "&=", or "|=".
    std::string op;
    ExpressionPtr value;
    bool provide{};
    bool hidden{};
    SourceLocation location;
};

// An input section description: `FILE(SECTION ...)`, possibly in KEEP.
struct InputPattern {
    std::string file{"*"};
    std::vector<std::string> exclude_files;
    std::vector<std::string> sections;
    bool keep{};
    enum class Sort : std::uint8_t { None, Name, Alignment } sort{};
    SourceLocation location;
};

// BYTE, SHORT, LONG, or QUAD.
struct Data {
    unsigned size{};
    ExpressionPtr value;
    SourceLocation location;
};

struct Assert {
    ExpressionPtr condition;
    std::string message;
    SourceLocation location;
};

using SectionItem = std::variant<Assignment, InputPattern, Data, Assert>;

struct OutputSection {
    std::string name;
    ExpressionPtr address;
    ExpressionPtr load_address;
    ExpressionPtr alignment;
    bool noload{};
    std::vector<SectionItem> items;
    std::string region;
    std::string load_region;
    std::vector<std::string> phdrs;
    ExpressionPtr fill;
    SourceLocation location;
};

using Command = std::variant<Assignment, OutputSection, Assert>;

struct Phdr {
    std::string name;
    std::uint32_t type{};
    bool file_header{};
    bool program_headers{};
    ExpressionPtr address;
    ExpressionPtr flags;
    SourceLocation location;
};

struct Region {
    std::string name;
    ExpressionPtr origin;
    ExpressionPtr length;
    SourceLocation location;
};

struct Script {
    std::optional<std::string> entry;
    std::vector<std::filesystem::path> search_directories;
    std::vector<std::string> externs;
    std::vector<Region> regions;
    std::vector<Phdr> phdrs;
    bool has_sections{};
    // Assignments and assertions outside SECTIONS and the commands inside
    // it, in script order.
    std::vector<Command> commands;
    std::vector<std::string> output_formats;
};

// Parses a script into `script`, following INCLUDE through `search` (and
// the directories SEARCH_DIR adds); false after a diagnostic.
[[nodiscard]] bool parse(const SourceFile& file, Script& script,
                         const std::vector<std::filesystem::path>& search,
                         SourceManager& manager, Diagnostics& diagnostics);

// Parses `NAME=EXPRESSION` (from --defsym) as an assignment.
[[nodiscard]] bool parse_definition(const SourceFile& file, Script& script,
                                    SourceManager& manager, Diagnostics& diagnostics);

// GNU ld wildcard matching: `*`, `?`, and `[...]` classes, where `*` also
// matches `/`.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text);

} // namespace cross::linker::script
