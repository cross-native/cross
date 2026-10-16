// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/source.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cross::assembler {

// GNU-syntax assembler core: statements, expressions, sections, symbols,
// layout, DWARF line and call-frame tables, and ELF relocatable output.
// Instruction syntax, encodings, and relocation types belong to a Target.

struct SymbolId {
    std::uint32_t value{};
    friend bool operator==(SymbolId, SymbolId) = default;
};
struct SectionId {
    std::uint32_t value{};
    friend bool operator==(SectionId, SectionId) = default;
};
struct ExpressionId {
    std::uint32_t value{};
    friend bool operator==(ExpressionId, ExpressionId) = default;
};

// Fixup kinds below target_base are generic data fields; targets number
// their own kinds from target_base.
struct FixupKind {
    std::uint16_t value{};
    static constexpr std::uint16_t target_base = 256;
    friend bool operator==(FixupKind, FixupKind) = default;
};
namespace fixup {
inline constexpr FixupKind data1{1};
inline constexpr FixupKind data2{2};
inline constexpr FixupKind data4{4};
inline constexpr FixupKind data8{8};
// A four-byte field holding a symbol minus the field's own address.
inline constexpr FixupKind data4_pc{20};
} // namespace fixup

namespace elf {
inline constexpr std::uint32_t sht_progbits = 1;
inline constexpr std::uint32_t sht_symtab = 2;
inline constexpr std::uint32_t sht_strtab = 3;
inline constexpr std::uint32_t sht_rela = 4;
inline constexpr std::uint32_t sht_note = 7;
inline constexpr std::uint32_t sht_nobits = 8;
inline constexpr std::uint32_t sht_rel = 9;
inline constexpr std::uint32_t sht_init_array = 14;
inline constexpr std::uint32_t sht_fini_array = 15;
inline constexpr std::uint32_t sht_preinit_array = 16;
inline constexpr std::uint32_t sht_group = 17;
inline constexpr std::uint64_t shf_write = 0x1;
inline constexpr std::uint64_t shf_alloc = 0x2;
inline constexpr std::uint64_t shf_execinstr = 0x4;
inline constexpr std::uint64_t shf_merge = 0x10;
inline constexpr std::uint64_t shf_strings = 0x20;
inline constexpr std::uint64_t shf_info_link = 0x40;
inline constexpr std::uint64_t shf_group = 0x200;
inline constexpr std::uint64_t shf_tls = 0x400;
inline constexpr std::uint64_t shf_gnu_retain = 0x200000;
inline constexpr std::uint64_t shf_exclude = 0x80000000;
inline constexpr std::uint8_t stt_notype = 0;
inline constexpr std::uint8_t stt_object = 1;
inline constexpr std::uint8_t stt_func = 2;
inline constexpr std::uint8_t stt_section = 3;
inline constexpr std::uint8_t stt_file = 4;
inline constexpr std::uint8_t stt_common = 5;
inline constexpr std::uint8_t stt_tls = 6;
inline constexpr std::uint8_t stt_gnu_ifunc = 10;
inline constexpr std::uint8_t stb_local = 0;
inline constexpr std::uint8_t stb_global = 1;
inline constexpr std::uint8_t stb_weak = 2;
inline constexpr std::uint8_t stv_default = 0;
inline constexpr std::uint8_t stv_internal = 1;
inline constexpr std::uint8_t stv_hidden = 2;
inline constexpr std::uint8_t stv_protected = 3;
} // namespace elf

enum class Operator : std::uint8_t {
    Negate, Complement, LogicalNot,
    Add, Subtract, Multiply, Divide, Remainder, ShiftLeft, ShiftRight,
    And, Or, Xor, OrNot, Equal, NotEqual, Less, LessEqual, Greater,
    GreaterEqual, LogicalAnd, LogicalOr,
};

struct Expression {
    enum class Kind : std::uint8_t { Constant, Symbol, Unary, Binary } kind{};
    Operator op{};
    std::int64_t value{};
    SymbolId symbol{};
    ExpressionId left{};
    ExpressionId right{};
    SourceLocation location;
};

// The relocatable form of an evaluated expression: add - subtract + constant.
struct Value {
    std::optional<SymbolId> add;
    std::optional<SymbolId> subtract;
    std::int64_t constant{};
    [[nodiscard]] bool absolute() const { return !add && !subtract; }
};

struct Symbol {
    enum class Binding : std::uint8_t { Unset, Local, Global, Weak };
    std::string name;
    // A label: its section, fragment, and offset within the fragment.
    std::optional<SectionId> section;
    std::uint32_t fragment{};
    std::uint64_t offset{};
    // An assignment (`sym = expr`, `.set`, `.equ`).
    std::optional<ExpressionId> variable;
    std::optional<ExpressionId> size;
    Binding binding{Binding::Unset};
    std::uint8_t type{elf::stt_notype};
    std::uint8_t visibility{elf::stv_default};
    std::uint8_t other{};
    // `.L` names, numeric local labels, and internal labels are omitted
    // from the symbol table unless a relocation must name them.
    bool temporary{};
    bool used_in_relocation{};
    bool common{};
    std::uint64_t common_size{};
    std::uint64_t common_alignment{};
    SourceLocation location;
    [[nodiscard]] bool defined() const {
        return section.has_value() || variable.has_value() || common;
    }
};

struct Fixup {
    std::uint32_t fragment{};
    std::uint32_t offset{};
    FixupKind kind{};
    ExpressionId expression{};
    SourceLocation location;
};

struct Fragment {
    enum class Kind : std::uint8_t { Data, Align, Leb, Fill, Org } kind{};
    std::vector<std::uint8_t> bytes;
    std::vector<Fixup> fixups;
    // Align: boundary, fill byte (or the target's no-op), and skip limit.
    std::uint64_t alignment{};
    std::uint64_t max_skip{};
    std::uint8_t fill{};
    bool code_fill{};
    // Leb, Fill (byte count), and Org (target offset) operands.
    ExpressionId expression{};
    bool is_signed{};
    SourceLocation location;
    // Layout results.
    std::uint64_t address{};
    std::uint64_t size{};
};

struct Section {
    std::string name;
    std::uint32_t type{elf::sht_progbits};
    std::uint64_t flags{};
    std::uint64_t entry_size{};
    std::uint64_t alignment{1};
    std::optional<SymbolId> group;
    bool comdat{};
    std::uint64_t unique{};
    SymbolId symbol{};
    std::vector<Fragment> fragments;
    std::uint64_t size{};
};

// A view of one statement's operand text with source positions.
class Cursor {
public:
    Cursor() = default;
    // An inexact cursor, over text a macro produced, reports every position
    // at `location`.
    Cursor(std::string_view text, SourceLocation location, bool exact = true)
        : text_(text), location_(location), exact_(exact) {}

    void skip_space();
    [[nodiscard]] bool at_end();
    [[nodiscard]] char peek();
    // Skips spaces, then consumes `token` if it is next.
    bool consume(std::string_view token);
    [[nodiscard]] SourceLocation location() const;
    [[nodiscard]] std::string_view rest() const {
        return text_.substr(position_);
    }
    [[nodiscard]] std::size_t position() const { return position_; }
    void set_position(std::size_t position) { position_ = position; }
    [[nodiscard]] std::string_view text() const { return text_; }
    // An unquoted name: letters, digits, '_', '.', and '$'.
    std::optional<std::string_view> identifier();
    // An unquoted or double-quoted symbol name.
    std::optional<std::string> symbol_name();
    // A double-quoted string with C escapes; nullopt when none is next.
    std::optional<std::string> string_literal(Diagnostics& diagnostics);

private:
    std::string_view text_;
    std::size_t position_{};
    SourceLocation location_;
    bool exact_{true};
};

struct Statement {
    std::string_view name;
    Cursor operands;
    SourceLocation location;
};

struct ObjectInfo {
    bool elf64{};
    bool big_endian{};
    std::uint16_t machine{};
    bool rela{};
};

// Call-frame facts of a target, in DWARF register numbers.
struct FrameFacts {
    unsigned code_alignment{1};
    int data_alignment{-4};
    unsigned return_address{};
    // The CIE's initial instructions and the CFA offset they establish.
    std::vector<std::uint8_t> initial_instructions;
    std::int64_t initial_cfa_offset{};
    // DW_EH_PE encoding of .eh_frame FDE addresses.
    std::uint8_t eh_pointer_encoding{};
};

struct FixupInfo {
    unsigned size{};
    bool pc_relative{};
};

struct RelocationChoice {
    std::uint32_t type{};
    // Name the symbol even when it is local and could be replaced by its
    // section symbol plus offset.
    bool keep_symbol{};
    // Added to the addend of a RELA relocation.
    std::int64_t rela_bias{};
};

struct Relocation {
    std::uint64_t offset{};
    std::uint32_t type{};
    // The relocated symbol, possibly a section symbol.
    SymbolId symbol{};
    // The complete addend, also for REL relocations whose addend is
    // stored in the section contents.
    std::int64_t addend{};
};

class Assembler;

class Target {
public:
    virtual ~Target() = default;
    [[nodiscard]] virtual ObjectInfo object() const = 0;
    [[nodiscard]] virtual std::uint32_t elf_flags() const = 0;
    [[nodiscard]] virtual FrameFacts frame_facts() const = 0;
    [[nodiscard]] virtual unsigned word_size() const { return 4; }
    // Whether `.align N` aligns to 2^N rather than N bytes.
    [[nodiscard]] virtual bool power_of_two_align() const { return true; }
    // The section type the target uses for a named section.
    [[nodiscard]] virtual std::uint32_t section_type(
        std::string_view, std::uint32_t type) const {
        return type;
    }
    [[nodiscard]] virtual std::optional<unsigned> dwarf_register(
        std::string_view name) const = 0;
    [[nodiscard]] virtual FixupInfo fixup_info(FixupKind kind) const = 0;

    // Parses and emits one instruction (or target macro) statement.
    virtual void instruction(Assembler& assembler, Statement& statement) = 0;
    // Handles a directive the target defines or overrides; false when the
    // generic directives should handle it.
    virtual bool directive(Assembler& assembler, Statement& statement) = 0;
    // Runs after the last statement and before layout.
    virtual void finish(Assembler& assembler) = 0;
    // Writes a resolved value, or a REL relocation's in-place addend, into
    // the bytes of a fixup; false after a diagnostic.
    virtual bool apply_fixup(Assembler& assembler, const Fixup& fixup,
                             std::int64_t value,
                             std::span<std::uint8_t> bytes,
                             bool relocated) const = 0;
    // The relocation for a fixup that refers to `symbol`; nullopt after a
    // diagnostic.
    virtual std::optional<RelocationChoice> relocation(
        Assembler& assembler, const Fixup& fixup, const Symbol& symbol,
        const Section* symbol_section) const = 0;
    // Orders one section's relocations, which arrive sorted by offset.
    virtual void order_relocations(std::vector<Relocation>&) const {}
};

struct Options {
    std::vector<std::filesystem::path> include_paths;
    // Directory of the main source for `.include` and `.incbin` lookups.
    std::filesystem::path source_directory;
    bool fatal_warnings{};
    bool no_warnings{};
    // The input is the compiler's own output, so object metadata may follow
    // the conventions the target's code generator keeps.
    bool compiler_output{};
};

// Assembles `sources`, in order, into one ELF relocatable object. Returns no
// object after any error; every diagnostic carries its source position.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, Target& target,
    const Options& options, SourceManager& manager, Diagnostics& diagnostics);

// The state of one assembly, which targets drive through this interface.
class Assembler {
public:
    Assembler(Target& target, const Options& options, SourceManager& sources,
              Diagnostics& diagnostics);
    ~Assembler();
    Assembler(const Assembler&) = delete;
    Assembler& operator=(const Assembler&) = delete;

    void error(SourceLocation location, std::string_view message);
    void warning(SourceLocation location, std::string_view message);
    [[nodiscard]] const ObjectInfo& object() const { return object_; }

    // Expressions.
    std::optional<ExpressionId> parse_expression(Cursor& cursor);
    ExpressionId constant(std::int64_t value, SourceLocation location);
    ExpressionId symbol_reference(SymbolId symbol, SourceLocation location);
    ExpressionId binary(Operator op, ExpressionId left, ExpressionId right);
    [[nodiscard]] const Expression& expression(ExpressionId id) const {
        return expressions_[id.value];
    }
    // The value now, before layout; nullopt (without a diagnostic) when
    // the expression is not yet absolute.
    [[nodiscard]] std::optional<std::int64_t> absolute_now(ExpressionId id);
    // Parses an expression that must be absolute now; nullopt after a
    // diagnostic.
    std::optional<std::int64_t> parse_absolute(Cursor& cursor);
    // The relocatable value of an expression at the current state. With
    // `keep_aliases`, a global symbol assigned a label-relative value stays
    // itself, so a relocation names the alias rather than its target.
    std::optional<Value> evaluate(ExpressionId id, bool report,
                                  bool keep_aliases = false);

    // Symbols and sections.
    SymbolId symbol(std::string_view name, SourceLocation location = {});
    SymbolId temporary_label();
    [[nodiscard]] Symbol& symbol(SymbolId id) { return symbols_[id.value]; }
    [[nodiscard]] const Symbol& symbol(SymbolId id) const {
        return symbols_[id.value];
    }
    void define_label(SymbolId id, SourceLocation location);
    void assign(SymbolId id, ExpressionId value, SourceLocation location);
    // `.set NAME, EXPR` and the equivalent directives.
    void assignment_statement(Statement& statement);
    SectionId section(std::string_view name, std::uint32_t type,
                      std::uint64_t flags, std::uint64_t entry_size = 0,
                      std::optional<SymbolId> group = std::nullopt,
                      bool comdat = false, std::uint64_t unique = 0);
    [[nodiscard]] Section& section(SectionId id) {
        return sections_[id.value];
    }
    [[nodiscard]] const Section& section(SectionId id) const {
        return sections_[id.value];
    }
    void switch_section(SectionId id);
    void push_section(SectionId id);
    void pop_section(SourceLocation location);

    // Emission into the current section.
    void emit_bytes(std::span<const std::uint8_t> bytes);
    void emit_integer(std::uint64_t value, unsigned size);
    // Records a fixup at the current position for the next emitted bytes.
    void add_fixup(FixupKind kind, ExpressionId expression,
                   SourceLocation location);
    // Emits `size` bytes holding `expression`, absolute or relocated.
    void emit_value(ExpressionId expression, unsigned size,
                    SourceLocation location);
    void emit_alignment(std::uint64_t alignment, std::optional<std::uint8_t> fill,
                        std::uint64_t max_skip);

    // Integers in the object's byte order.
    void write_integer(std::span<std::uint8_t> bytes, std::uint64_t value) const;
    [[nodiscard]] std::uint64_t read_integer(
        std::span<const std::uint8_t> bytes) const;

    std::optional<std::vector<std::uint8_t>> run(
        const std::vector<const SourceFile*>& sources);

private:
    struct Impl;
    friend struct Impl;
    Target& target_;
    Options options_;
    SourceManager& sources_;
    Diagnostics& diagnostics_;
    ObjectInfo object_;
    std::vector<Expression> expressions_;
    std::vector<Symbol> symbols_;
    std::vector<Section> sections_;
    SectionId current_{};
    std::unique_ptr<Impl> impl_;
};

} // namespace cross::assembler
