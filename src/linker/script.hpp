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
    // The digits of a hexadecimal literal without a K or M suffix, which a
    // fill pattern takes whole.
    std::string digits;
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
    enum class Sort : std::uint8_t { None, Name, Alignment, InitPriority } sort{};
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

// A fill pattern: `=FILL` after an output section or `FILL(...)` inside one.
struct Fill {
    ExpressionPtr value;
    SourceLocation location;
};

using SectionItem = std::variant<Assignment, InputPattern, Data, Assert, Fill>;

struct OutputSection {
    std::string name;
    ExpressionPtr address;
    ExpressionPtr load_address;
    ExpressionPtr alignment;
    ExpressionPtr subalign;
    // (NOLOAD) gives zero-filled space; (DSECT), (COPY), (INFO), and
    // (OVERLAY) give a section without memory.
    enum class Kind : std::uint8_t { Normal, NoLoad, NoAlloc } kind{};
    bool readonly{};
    // (TYPE = ...): the section type.
    ExpressionPtr type;
    // ONLY_IF_RO and ONLY_IF_RW.
    enum class Constraint : std::uint8_t { None, ReadOnly, ReadWrite } constraint{};
    // ALIGN_WITH_INPUT: the load address keeps the address's alignment gap.
    bool align_with_input{};
    // A section of an OVERLAY: the first, or a later one, whose load address
    // follows the previous section's.
    enum class Overlay : std::uint8_t { None, First, Later } overlay{};
    // The location counter after the section, for an OVERLAY's last one.
    ExpressionPtr update_dot;
    std::vector<SectionItem> items;
    std::string region;
    std::string load_region;
    std::vector<std::string> phdrs;
    std::optional<Fill> fill;
    SourceLocation location;
};

// INSERT AFTER or BEFORE an output section.
struct Insert {
    std::string section;
    bool before{};
    SourceLocation location;
};

using Command = std::variant<Assignment, OutputSection, Assert, Insert>;

struct Phdr {
    std::string name;
    std::uint32_t type{};
    bool file_header{};
    bool program_headers{};
    ExpressionPtr address;
    ExpressionPtr flags;
    SourceLocation location;
};

// Section attributes of MEMORY regions (GNU ld's R, W, X, A, and I or L).
enum : std::uint8_t {
    region_readonly = 1,
    region_writable = 2,
    region_executable = 4,
    region_allocated = 8,
    region_loaded = 16,
};

struct Region {
    std::string name;
    // REGION_ALIAS names.
    std::vector<std::string> aliases;
    // The attributes a section must have one of, and must have none of.
    std::uint8_t flags{};
    std::uint8_t not_flags{};
    ExpressionPtr origin;
    ExpressionPtr length;
    SourceLocation location;
};

// A file that INPUT, GROUP, or STARTUP names; `-lNAME` searches libraries.
struct ScriptInput {
    std::string name;
    bool library{};
    // The directory of the script that names it, searched first when that
    // is an implicit script.
    std::filesystem::path directory;
    SourceLocation location;
};

struct CrossReferences {
    std::vector<std::string> sections;
    // NOCROSSREFS_TO: only references to the first section are prohibited.
    bool to{};
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
    // INPUT and GROUP files, and the STARTUP file first.
    std::vector<ScriptInput> inputs;
    std::vector<CrossReferences> cross_references;
    bool has_insert{};
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
