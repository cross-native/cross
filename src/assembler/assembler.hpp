// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/source.hpp"

#include <algorithm>
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
// layout, DWARF line and call-frame tables, and ELF, COFF, and Mach-O
// relocatable output. Instruction syntax, encodings, and relocation types
// belong to a Target.

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
// A four- or eight-byte field holding a symbol minus the field's address.
inline constexpr FixupKind data4_pc{20};
inline constexpr FixupKind data8_pc{21};
// A four-byte offset of a symbol from the start of its section (DWARF
// section offsets, COFF `.secrel32`).
inline constexpr FixupKind section_offset4{22};
// COFF: the two-byte section number of a symbol (`.secidx`) and a symbol's
// four-byte address relative to the image base (`.rva`).
inline constexpr FixupKind section_index2{23};
inline constexpr FixupKind image_offset4{24};
} // namespace fixup

enum class Format : std::uint8_t { Elf, Coff, MachO };

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
inline constexpr std::uint32_t sht_gnu_attributes = 0x6ffffff5;
inline constexpr std::uint64_t shf_write = 0x1;
inline constexpr std::uint64_t shf_alloc = 0x2;
inline constexpr std::uint64_t shf_execinstr = 0x4;
inline constexpr std::uint64_t shf_merge = 0x10;
inline constexpr std::uint64_t shf_strings = 0x20;
inline constexpr std::uint64_t shf_info_link = 0x40;
inline constexpr std::uint64_t shf_link_order = 0x80;
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

namespace coff {
inline constexpr std::uint32_t scn_cnt_code = 0x20;
inline constexpr std::uint32_t scn_cnt_initialized_data = 0x40;
inline constexpr std::uint32_t scn_cnt_uninitialized_data = 0x80;
inline constexpr std::uint32_t scn_lnk_info = 0x200;
inline constexpr std::uint32_t scn_lnk_remove = 0x800;
inline constexpr std::uint32_t scn_lnk_comdat = 0x1000;
inline constexpr std::uint32_t scn_align_mask = 0x00f00000;
inline constexpr std::uint32_t scn_mem_discardable = 0x02000000;
inline constexpr std::uint32_t scn_mem_shared = 0x10000000;
inline constexpr std::uint32_t scn_mem_execute = 0x20000000;
inline constexpr std::uint32_t scn_mem_read = 0x40000000;
inline constexpr std::uint32_t scn_mem_write = 0x80000000;
inline constexpr std::uint8_t sym_class_external = 2;
inline constexpr std::uint8_t sym_class_static = 3;
inline constexpr std::uint8_t sym_class_label = 6;
inline constexpr std::uint8_t sym_class_file = 103;
inline constexpr std::uint8_t sym_class_weak_external = 105;
inline constexpr std::uint16_t sym_type_function = 0x20;
inline constexpr std::uint8_t comdat_select_noduplicates = 1;
inline constexpr std::uint8_t comdat_select_any = 2;
inline constexpr std::uint8_t comdat_select_same_size = 3;
inline constexpr std::uint8_t comdat_select_exact_match = 4;
inline constexpr std::uint8_t comdat_select_associative = 5;
inline constexpr std::uint8_t comdat_select_largest = 6;
inline constexpr std::uint8_t comdat_select_newest = 7;
} // namespace coff

namespace macho {
inline constexpr std::uint32_t s_regular = 0x0;
inline constexpr std::uint32_t s_zerofill = 0x1;
inline constexpr std::uint32_t s_cstring_literals = 0x2;
inline constexpr std::uint32_t s_4byte_literals = 0x3;
inline constexpr std::uint32_t s_8byte_literals = 0x4;
inline constexpr std::uint32_t s_literal_pointers = 0x5;
inline constexpr std::uint32_t s_non_lazy_symbol_pointers = 0x6;
inline constexpr std::uint32_t s_lazy_symbol_pointers = 0x7;
inline constexpr std::uint32_t s_symbol_stubs = 0x8;
inline constexpr std::uint32_t s_mod_init_func_pointers = 0x9;
inline constexpr std::uint32_t s_mod_term_func_pointers = 0xa;
inline constexpr std::uint32_t s_coalesced = 0xb;
inline constexpr std::uint32_t s_gb_zerofill = 0xc;
inline constexpr std::uint32_t s_interposing = 0xd;
inline constexpr std::uint32_t s_16byte_literals = 0xe;
inline constexpr std::uint32_t s_dtrace_dof = 0xf;
inline constexpr std::uint32_t s_lazy_dylib_symbol_pointers = 0x10;
inline constexpr std::uint32_t s_thread_local_regular = 0x11;
inline constexpr std::uint32_t s_thread_local_zerofill = 0x12;
inline constexpr std::uint32_t s_thread_local_variables = 0x13;
inline constexpr std::uint32_t s_thread_local_variable_pointers = 0x14;
inline constexpr std::uint32_t s_thread_local_init_function_pointers = 0x15;
inline constexpr std::uint32_t section_type = 0xff;
inline constexpr std::uint32_t s_attr_pure_instructions = 0x80000000;
inline constexpr std::uint32_t s_attr_no_toc = 0x40000000;
inline constexpr std::uint32_t s_attr_strip_static_syms = 0x20000000;
inline constexpr std::uint32_t s_attr_no_dead_strip = 0x10000000;
inline constexpr std::uint32_t s_attr_live_support = 0x08000000;
inline constexpr std::uint32_t s_attr_self_modifying_code = 0x04000000;
inline constexpr std::uint32_t s_attr_debug = 0x02000000;
inline constexpr std::uint32_t s_attr_some_instructions = 0x00000400;
} // namespace macho

enum class Operator : std::uint8_t {
    Negate, Complement, LogicalNot,
    Add, Subtract, Multiply, Divide, Remainder, ShiftLeft, ShiftRight,
    And, Or, Xor, OrNot, Equal, NotEqual, Less, LessEqual, Greater,
    GreaterEqual, LogicalAnd, LogicalOr,
};

struct Expression {
    enum class Kind : std::uint8_t { Constant, Symbol, Unary, Binary } kind{};
    Operator op{};
    // A Symbol reference's relocation specifier (`sym@PLT`), numbered by
    // the target; 0 for none.
    std::uint8_t specifier{};
    std::int64_t value{};
    SymbolId symbol{};
    ExpressionId left{};
    ExpressionId right{};
    SourceLocation location;
};

// The relocatable form of an evaluated expression: add - subtract + constant,
// with the specifier of the added symbol.
struct Value {
    std::optional<SymbolId> add;
    std::optional<SymbolId> subtract;
    std::int64_t constant{};
    std::uint8_t specifier{};
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
    // `.L` names (`L` in Mach-O), numeric local labels, and internal labels
    // are omitted from the symbol table unless a relocation must name them.
    bool temporary{};
    bool used_in_relocation{};
    bool common{};
    std::uint64_t common_size{};
    std::uint64_t common_alignment{};
    // COFF `.def` attributes: storage class (0 when unset) and type.
    std::uint8_t storage_class{};
    std::uint16_t coff_type{};
    // Mach-O attributes.
    bool private_extern{};
    bool weak_definition{};
    bool weak_reference{};
    bool no_dead_strip{};
    bool alt_entry{};
    // The order of label definitions, which places labels that share an
    // address.
    std::uint32_t definition{};
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
    enum class Kind : std::uint8_t { Data, Align, Leb, Fill, Org, Relaxable } kind{};
    // The subsection that holds the fragment; layout orders a section's
    // subsections by number.
    std::int64_t subsection{};
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
    // Relaxable: one instruction whose short form in `bytes` has one
    // PC-relative one-byte fixup; layout replaces it with `relaxed` and
    // `relaxed_fixup` (an offset into `relaxed`) when the value does not
    // fit or needs a relocation, and the fragment becomes Data.
    std::vector<std::uint8_t> relaxed;
    Fixup relaxed_fixup;
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
    // ELF group signature, or the COFF COMDAT symbol.
    std::optional<SymbolId> group;
    bool comdat{};
    std::uint64_t unique{};
    // COFF characteristics, or Mach-O section flags (type and attributes).
    std::uint32_t characteristics{};
    // Mach-O segment name; `name` is the section name.
    std::string segment;
    // COFF COMDAT selection; associative sections name the section of the
    // COMDAT symbol through `group`.
    std::uint8_t selection{};
    // Format-neutral facts: the section holds instructions, or occupies no
    // file space.
    bool code{};
    bool zero_fill{};
    // An instruction was emitted into the section.
    bool has_instructions{};
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
    // The ELF e_machine or COFF Machine value.
    std::uint16_t machine{};
    bool rela{};
    Format format{Format::Elf};
    // Mach-O CPU type and subtype.
    std::uint32_t cpu_type{};
    std::uint32_t cpu_subtype{};
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
    // Mach-O: the field is PC-relative, its size as a power of two, and
    // the relocation names a symbol rather than a section (whose section
    // symbol is `symbol`).
    bool pc_relative{};
    std::uint8_t length{};
    bool external{true};
    // ELF: the fixup's place among its section's fixups in the order they
    // were created, and the symbol and addend it named before a local
    // symbol became its section's symbol.
    std::uint32_t sequence{};
    SymbolId target{};
    std::int64_t target_addend{};
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
    // A private label name of the target, beyond the format's `.L` (`L` in
    // Mach-O); private labels stay out of the symbol table.
    [[nodiscard]] virtual bool private_label(std::string_view) const { return false; }
    // Whether `.file` alone, without `.loc` rows, yields a line table, as in
    // LLVM MC; GNU as writes none.
    [[nodiscard]] virtual bool line_table_without_rows() const { return true; }
    // The alignment of `.lcomm NAME, SIZE` without an alignment operand.
    [[nodiscard]] virtual std::uint64_t local_common_alignment(std::uint64_t) const {
        return 1;
    }
    // The section type and flags the target uses for a named section.
    [[nodiscard]] virtual std::uint32_t section_type(
        std::string_view, std::uint32_t type) const {
        return type;
    }
    [[nodiscard]] virtual std::uint64_t section_flags(
        std::string_view, std::uint64_t flags) const {
        return flags;
    }
    [[nodiscard]] virtual std::optional<unsigned> dwarf_register(
        std::string_view name) const = 0;
    [[nodiscard]] virtual FixupInfo fixup_info(FixupKind kind) const = 0;
    // The number of a relocation specifier spelled after '@' (`sym@PLT`).
    [[nodiscard]] virtual std::optional<std::uint8_t> specifier(
        std::string_view) const {
        return std::nullopt;
    }
    // Fills alignment padding in code sections.
    virtual void code_padding(std::span<std::uint8_t> bytes) const {
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    }

    // Parses and emits one instruction (or target macro) statement.
    virtual void instruction(Assembler& assembler, Statement& statement) = 0;
    // Handles a directive the target defines or overrides; false when the
    // generic directives should handle it.
    virtual bool directive(Assembler& assembler, Statement& statement) = 0;
    // A source label, or a `.` reference, was defined at the current
    // position.
    virtual void label(Assembler&, SymbolId) {}
    // Runs before a directive emits something other than an instruction or
    // changes the section, and after the last statement.
    virtual void flush(Assembler&) {}
    // Runs after the last statement and before layout.
    virtual void finish(Assembler& assembler) = 0;
    // Runs after layout and the call-frame tables, before the line table.
    virtual void finish_tables(Assembler&) {}
    // Writes a resolved value, or a REL relocation's in-place addend, into
    // the bytes of a fixup; false after a diagnostic.
    virtual bool apply_fixup(Assembler& assembler, const Fixup& fixup,
                             std::int64_t value,
                             std::span<std::uint8_t> bytes,
                             bool relocated) const = 0;
    // The relocation for a fixup that refers to `symbol`; nullopt after a
    // diagnostic.
    virtual std::optional<RelocationChoice> relocation(
        Assembler& assembler, const Fixup& fixup, const Value& value,
        const Symbol& symbol, const Section* symbol_section) const = 0;
    // Orders one section's REL relocations, which arrive in the order their
    // fixups were created; by default by offset.
    virtual void order_relocations(std::vector<Relocation>& relocations) const {
        std::stable_sort(relocations.begin(), relocations.end(),
                         [](const Relocation& left, const Relocation& right) {
                             return left.offset < right.offset;
                         });
    }
    // Mach-O: resolves one fixup of `section` into its field value and
    // relocations; false after a diagnostic.
    virtual bool macho_relocation(Assembler&, SectionId, const Fixup&, std::uint64_t /*place*/,
                                  std::uint64_t /*address*/, const Value&, std::int64_t& /*field*/,
                                  std::vector<Relocation>&) const {
        return false;
    }
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
    // GNU as -O: 0 keeps every hazard nop the target may need, 1 removes
    // the unneeded ones, 2 also fills delay slots.
    unsigned optimize{2};
    // Directives processed before the first source, for command options
    // that mean what a directive means (MIPS -mgp32 is `.module gp=32`).
    std::vector<std::string> directives;
};

// Assembles `sources`, in order, into one relocatable object. Returns no
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
    [[nodiscard]] const Options& options() const { return options_; }
    [[nodiscard]] Diagnostics& diagnostics() { return diagnostics_; }

    // Expressions.
    std::optional<ExpressionId> parse_expression(Cursor& cursor);
    ExpressionId constant(std::int64_t value, SourceLocation location);
    ExpressionId symbol_reference(SymbolId symbol, SourceLocation location,
                                  std::uint8_t specifier = 0);
    ExpressionId binary(Operator op, ExpressionId left, ExpressionId right);
    [[nodiscard]] const Expression& expression(ExpressionId id) const {
        return expressions_[id.value];
    }
    // The value now, before layout; nullopt (without a diagnostic) when
    // the expression is not yet absolute.
    [[nodiscard]] std::optional<std::int64_t> absolute_now(ExpressionId id);
    // The value when it involves no label, as LLVM MC folds an operand while
    // parsing; nullopt otherwise.
    [[nodiscard]] std::optional<std::int64_t> constant_now(ExpressionId id);
    // Parses an expression that must be absolute now; nullopt after a
    // diagnostic.
    std::optional<std::int64_t> parse_absolute(Cursor& cursor);
    // The relocatable value of an expression at the current state. With
    // `keep_aliases`, a global symbol assigned a label-relative value stays
    // itself, so a relocation names the alias rather than its target.
    // `in_set` evaluates the value of an assignment, whose label differences
    // always fold (Mach-O otherwise folds only differences within an atom).
    std::optional<Value> evaluate(ExpressionId id, bool report,
                                  bool keep_aliases = false, bool in_set = false);

    // Symbols and sections.
    SymbolId symbol(std::string_view name, SourceLocation location = {});
    SymbolId temporary_label();
    [[nodiscard]] Symbol& symbol(SymbolId id) { return symbols_[id.value]; }
    [[nodiscard]] const Symbol& symbol(SymbolId id) const {
        return symbols_[id.value];
    }
    [[nodiscard]] const std::vector<Symbol>& symbols() const { return symbols_; }
    void define_label(SymbolId id, SourceLocation location);
    // Moves a label of the current section to the current position.
    void move_label(SymbolId id);
    // Adds an STT_FILE symbol, as `.file NAME` does.
    void add_file_symbol(std::string name);
    void assign(SymbolId id, ExpressionId value, SourceLocation location);
    // `.set NAME, EXPR` and the equivalent directives.
    void assignment_statement(Statement& statement);
    // An ELF section.
    SectionId section(std::string_view name, std::uint32_t type,
                      std::uint64_t flags, std::uint64_t entry_size = 0,
                      std::optional<SymbolId> group = std::nullopt,
                      bool comdat = false, std::uint64_t unique = 0);
    // A COFF section with its characteristics and COMDAT symbol.
    SectionId coff_section(std::string_view name, std::uint32_t characteristics,
                           std::optional<SymbolId> comdat = std::nullopt,
                           std::uint8_t selection = 0);
    // A Mach-O section; flags apply when it is created.
    SectionId macho_section(std::string_view segment, std::string_view name,
                            std::uint32_t flags);
    [[nodiscard]] Section& section(SectionId id) {
        return sections_[id.value];
    }
    [[nodiscard]] const Section& section(SectionId id) const {
        return sections_[id.value];
    }
    [[nodiscard]] const std::vector<Section>& sections() const { return sections_; }
    [[nodiscard]] SectionId current_section() const { return current_; }
    [[nodiscard]] std::int64_t current_subsection() const;
    void switch_section(SectionId id, std::int64_t subsection = 0);
    void push_section(SectionId id, std::int64_t subsection = 0);
    void pop_section(SourceLocation location);

    // Emission into the current section.
    void emit_bytes(std::span<const std::uint8_t> bytes);
    // Emits bytes the target inserts on its own, such as hazard padding,
    // which do not take a pending `.loc` row.
    void emit_padding(std::span<const std::uint8_t> bytes);
    void emit_integer(std::uint64_t value, unsigned size);
    // Records a fixup at the current position for the next emitted bytes.
    void add_fixup(FixupKind kind, ExpressionId expression,
                   SourceLocation location);
    // Emits `size` bytes holding `expression`, absolute or relocated.
    void emit_value(ExpressionId expression, unsigned size,
                    SourceLocation location);
    void emit_alignment(std::uint64_t alignment, std::optional<std::uint8_t> fill,
                        std::uint64_t max_skip);
    // Emits an instruction whose short form `bytes` holds a one-byte
    // PC-relative field at `offset`, and whose long form `relaxed` holds the
    // same reference with `relaxed_kind` at `relaxed_offset`.
    void emit_relaxable(std::vector<std::uint8_t> bytes, std::uint32_t offset,
                        FixupKind kind, std::vector<std::uint8_t> relaxed,
                        std::uint32_t relaxed_offset, FixupKind relaxed_kind,
                        ExpressionId expression, SourceLocation location);
    // Materializes a pending `.loc` row before an instruction and marks the
    // current section as holding instructions.
    void begin_instruction();
    // Mach-O: a section's address within the object, after layout.
    [[nodiscard]] std::uint64_t section_address(SectionId id) const;

    // The final bytes of a section that occupies file space.
    [[nodiscard]] std::vector<std::uint8_t> section_contents(SectionId id) const;

    // Integers in the object's byte order.
    void write_integer(std::span<std::uint8_t> bytes, std::uint64_t value) const;
    [[nodiscard]] std::uint64_t read_integer(
        std::span<const std::uint8_t> bytes) const;

    // After layout: a label's address within its section, the section of a
    // symbol (following assignments), and, for Mach-O, the atom of a label:
    // the latest non-temporary label at or before it in its section.
    [[nodiscard]] std::uint64_t address(const Symbol& symbol) const;
    [[nodiscard]] const Symbol* base_symbol(const Symbol& symbol);
    [[nodiscard]] std::optional<SymbolId> atom(SymbolId id) const;

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
