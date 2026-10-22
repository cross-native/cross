// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/assembler.hpp"

#include "assembler/coff_writer.hpp"
#include "assembler/elf_writer.hpp"
#include "assembler/macho_writer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace cross::assembler {
namespace {

bool identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '.' || c == '$';
}

bool identifier_char(char c) {
    return identifier_start(c) || (c >= '0' && c <= '9');
}

bool digit(char c) { return c >= '0' && c <= '9'; }

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

unsigned uleb_size(std::uint64_t value) {
    unsigned size = 0;
    do {
        value >>= 7U;
        ++size;
    } while (value != 0);
    return size;
}

void append_uleb(std::vector<std::uint8_t>& out, std::uint64_t value,
                 unsigned pad_to = 0) {
    unsigned count = 0;
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        ++count;
        if (value != 0 || count < pad_to) byte |= 0x80U;
        out.push_back(byte);
    } while (value != 0);
    if (count < pad_to) {
        for (; count < pad_to - 1; ++count) out.push_back(0x80);
        out.push_back(0x00);
    }
}

void append_sleb(std::vector<std::uint8_t>& out, std::int64_t value,
                 unsigned pad_to = 0) {
    bool more = true;
    unsigned count = 0;
    while (more) {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        ++count;
        more = !((value == 0 && (byte & 0x40U) == 0) ||
                 (value == -1 && (byte & 0x40U) != 0));
        if (more || count < pad_to) byte |= 0x80U;
        out.push_back(byte);
    }
    if (count < pad_to) {
        const std::uint8_t pad = value < 0 ? 0x7f : 0x00;
        for (; count < pad_to - 1; ++count) {
            out.push_back(static_cast<std::uint8_t>(pad | 0x80U));
        }
        out.push_back(pad);
    }
}

bool fits(std::int64_t value, unsigned size) {
    if (size >= 8) return true;
    const auto bits = size * 8;
    const auto low = -(std::int64_t{1} << (bits - 1));
    const auto high = static_cast<std::int64_t>((std::uint64_t{1} << bits) - 1);
    return value >= low && value <= high;
}

constexpr std::uint8_t dw_cfa_advance_loc = 0x40;
constexpr std::uint8_t dw_cfa_offset = 0x80;
constexpr std::uint8_t dw_cfa_restore = 0xc0;

} // namespace

// ---------------------------------------------------------------------------
// Cursor

void Cursor::skip_space() {
    while (position_ < text_.size() && space(text_[position_])) ++position_;
}

bool Cursor::at_end() {
    skip_space();
    return position_ >= text_.size();
}

char Cursor::peek() {
    skip_space();
    return position_ < text_.size() ? text_[position_] : '\0';
}

bool Cursor::consume(std::string_view token) {
    skip_space();
    if (text_.substr(position_).starts_with(token)) {
        position_ += token.size();
        return true;
    }
    return false;
}

SourceLocation Cursor::location() const {
    auto result = location_;
    if (exact_) {
        result.offset += position_;
        result.column += static_cast<unsigned>(position_);
    }
    return result;
}

std::optional<std::string_view> Cursor::identifier() {
    skip_space();
    if (position_ >= text_.size() || !identifier_start(text_[position_])) {
        return std::nullopt;
    }
    const auto start = position_;
    while (position_ < text_.size() && identifier_char(text_[position_])) {
        ++position_;
    }
    return text_.substr(start, position_ - start);
}

std::optional<std::string> Cursor::symbol_name() {
    skip_space();
    if (position_ < text_.size() && text_[position_] == '"') {
        std::string result;
        std::size_t index = position_ + 1;
        while (index < text_.size() && text_[index] != '"') {
            if (text_[index] == '\\' && index + 1 < text_.size()) ++index;
            result.push_back(text_[index]);
            ++index;
        }
        if (index >= text_.size()) return std::nullopt;
        position_ = index + 1;
        return result;
    }
    if (auto name = identifier()) return std::string(*name);
    return std::nullopt;
}

std::optional<std::string> Cursor::string_literal(Diagnostics& diagnostics) {
    skip_space();
    if (position_ >= text_.size() || text_[position_] != '"') {
        return std::nullopt;
    }
    std::string result;
    std::size_t index = position_ + 1;
    while (index < text_.size() && text_[index] != '"') {
        char c = text_[index++];
        if (c != '\\' || index >= text_.size()) {
            result.push_back(c);
            continue;
        }
        c = text_[index++];
        switch (c) {
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'v': result.push_back('\v'); break;
        case 'a': result.push_back('\a'); break;
        case 'e': result.push_back('\x1b'); break;
        case 'x':
        case 'X': {
            unsigned value = 0;
            while (index < text_.size() && std::isxdigit(
                       static_cast<unsigned char>(text_[index]))) {
                const char h = text_[index++];
                value = (value << 4U) |
                        static_cast<unsigned>(digit(h) ? h - '0'
                                              : (h | 0x20) - 'a' + 10);
            }
            result.push_back(static_cast<char>(value & 0xffU));
            break;
        }
        default:
            if (c >= '0' && c <= '7') {
                unsigned value = static_cast<unsigned>(c - '0');
                for (int count = 0; count < 2 && index < text_.size() &&
                                    text_[index] >= '0' && text_[index] <= '7';
                     ++count) {
                    value = value * 8 + static_cast<unsigned>(text_[index++] - '0');
                }
                result.push_back(static_cast<char>(value & 0xffU));
            } else {
                result.push_back(c);
            }
            break;
        }
    }
    if (index >= text_.size()) {
        diagnostics.error(location(), "unterminated string");
        position_ = text_.size();
        return std::nullopt;
    }
    position_ = index + 1;
    return result;
}

// ---------------------------------------------------------------------------
// Internal state

struct Assembler::Impl {
    Assembler& as;

    explicit Impl(Assembler& owner) : as(owner) {}

    // Statements -----------------------------------------------------------
    struct Conditional {
        bool active{};
        bool taken{};
        bool else_seen{};
    };
    std::vector<Conditional> conditionals;
    struct Macro {
        struct Parameter {
            std::string name;
            std::string default_value;
            bool required{};
            bool vararg{};
        };
        std::string name;
        std::vector<Parameter> parameters;
        std::vector<std::string> body;
        SourceLocation location;
    };
    std::unordered_map<std::string, Macro> macros;
    struct Capture {
        enum class Kind { Macro, Repeat } kind{};
        Macro macro;
        std::uint64_t count{};
        unsigned depth{};
        SourceLocation location;
    };
    std::optional<Capture> capture;
    unsigned expansion_depth{};
    std::uint64_t macro_counter{};
    std::uint64_t local_counter{};
    bool exit_macro{};
    // `.altmacro`: `%expr` and `<text>` arguments, parameters named without
    // a backslash, and `LOCAL` names.
    bool alternate_macro{};
    bool in_block_comment{};
    std::uint32_t definitions{};
    // Mach-O atom-defining labels per section in (fragment, offset,
    // definition) order, computed when `atom_definitions` labels existed.
    std::vector<std::vector<std::tuple<std::uint32_t, std::uint64_t, std::uint32_t, SymbolId>>> atoms;
    std::uint32_t atom_definitions{~0U};
    void build_atoms() {
        if (atom_definitions == definitions && atoms.size() == as.sections_.size()) return;
        atom_definitions = definitions;
        atoms.assign(as.sections_.size(), {});
        for (std::uint32_t index = 0; index < as.symbols_.size(); ++index) {
            const auto& entry = as.symbols_[index];
            if (!entry.section || entry.temporary || entry.alt_entry ||
                entry.type == elf::stt_section) {
                continue;
            }
            atoms[entry.section->value].emplace_back(entry.fragment, entry.offset,
                                                     entry.definition, SymbolId{index});
        }
        for (auto& list : atoms) std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
            return std::tuple{std::get<0>(a), std::get<1>(a), std::get<2>(a)} <
                   std::tuple{std::get<0>(b), std::get<1>(b), std::get<2>(b)};
        });
    }
    // COFF: the symbol of an open `.def`.
    std::optional<SymbolId> coff_definition;
    // Mach-O: `.subsections_via_symbols`.
    bool subsections_via_symbols{};

    // Sections ---------------------------------------------------------------
    std::map<std::tuple<std::string, std::uint32_t, std::uint64_t>, SectionId>
        section_map;
    // A section and subsection, for `.previous` and the section stack.
    struct Position {
        SectionId section{};
        std::int64_t subsection{};
        friend bool operator==(const Position&, const Position&) = default;
    };
    std::vector<std::pair<Position, Position>> section_stack;
    Position previous{};
    bool has_subsections{};
    std::unordered_map<std::string, SymbolId> symbol_map;
    std::unordered_map<std::uint64_t, std::uint64_t> local_label_instances;
    std::vector<bool> resolving;

    // DWARF line table ----------------------------------------------------------
    struct LineFile {
        std::string name;
        unsigned directory{};
        std::optional<std::array<std::uint8_t, 16>> md5;
    };
    struct LineRow {
        SymbolId label{};
        unsigned file{1};
        unsigned line{};
        unsigned column{};
        std::uint8_t flags{};
        unsigned isa{};
        unsigned discriminator{};
    };
    static constexpr std::uint8_t is_stmt = 1;
    static constexpr std::uint8_t basic_block = 2;
    static constexpr std::uint8_t prologue_end = 4;
    static constexpr std::uint8_t epilogue_begin = 8;
    bool line_table{};
    std::string compilation_directory;
    std::vector<std::string> line_directories;
    std::vector<LineFile> line_files;
    std::optional<LineFile> root_file;
    bool has_all_md5{true};
    std::optional<LineRow> pending_row;
    bool loc_is_stmt{true};
    std::vector<std::pair<SectionId, std::vector<LineRow>>> line_sections;
    std::vector<std::string> file_symbols;
    std::vector<std::uint32_t> file_symbol_order;

    // Call frames ------------------------------------------------------------------
    struct FrameInstruction {
        enum class Op : std::uint8_t {
            DefCfa, DefCfaRegister, DefCfaOffset, AdjustCfaOffset, Offset,
            RelOffset, ValOffset, Restore, Undefined, SameValue, Register,
            RememberState, RestoreState, Escape,
        } op{};
        SymbolId label{};
        unsigned reg{};
        unsigned reg2{};
        std::int64_t offset{};
        std::vector<std::uint8_t> bytes;
        SourceLocation location;
    };
    struct Frame {
        SectionId section{};
        SymbolId begin{};
        std::optional<SymbolId> end;
        std::vector<FrameInstruction> instructions;
        bool simple{};
        bool signal{};
        unsigned return_address{};
        SourceLocation location;
    };
    std::vector<Frame> frames;
    std::optional<std::size_t> open_frame;
    bool eh_frame{true};
    bool debug_frame{};

    // Output relocations per section.
    std::vector<std::vector<Relocation>> relocations;
    // Mach-O section addresses, set after the final layout.
    std::vector<std::uint64_t> section_addresses;
    bool layout_known{};

    SectionId add_section(Section entry, std::tuple<std::string, std::uint32_t, std::uint64_t> key) {
        const SectionId id{static_cast<std::uint32_t>(as.sections_.size())};
        entry.symbol = SymbolId{static_cast<std::uint32_t>(as.symbols_.size())};
        Symbol section_symbol;
        section_symbol.type = elf::stt_section;
        section_symbol.binding = Symbol::Binding::Local;
        section_symbol.section = id;
        as.symbols_.push_back(std::move(section_symbol));
        resolving.push_back(false);
        entry.fragments.emplace_back();
        as.sections_.push_back(std::move(entry));
        relocations.emplace_back();
        section_map.emplace(std::move(key), id);
        return id;
    }

    // ---------------------------------------------------------------------------
    void process_text(std::string_view text, const SourceFile* file,
                      std::size_t base_offset, unsigned first_line,
                      std::optional<SourceLocation> origin);
    void process_line(std::string_view line, SourceLocation location,
                      bool exact);
    void statement(std::string_view text, SourceLocation location, bool exact);
    bool conditional_directive(std::string_view name, Statement& statement);
    void directive(Statement& statement);
    void expand_macro(const Macro& macro, Statement& statement);
    void define_numeric_label(std::uint64_t number, SourceLocation location);
    std::string numeric_label_name(std::uint64_t number, bool forward);

    // Directives.
    void data(Statement& statement, unsigned size);
    void leb(Statement& statement, bool is_signed);
    void ascii(Statement& statement, bool terminate);
    void floating(Statement& statement, unsigned size);
    void reserve(Statement& statement, bool zero);
    void fill(Statement& statement);
    void align(Statement& statement, bool power);
    void org(Statement& statement);
    void incbin(Statement& statement);
    void include(Statement& statement);
    void section_directive(Statement& statement, bool push);
    void symbol_binding(Statement& statement, Symbol::Binding binding);
    void symbol_visibility(Statement& statement, std::uint8_t visibility);
    void type(Statement& statement);
    void size(Statement& statement);
    void common(Statement& statement, bool local);
    void file(Statement& statement);
    void loc(Statement& statement);
    void cfi(Statement& statement);
    void ident(Statement& statement);
    bool coff_directive(std::string_view name, Statement& statement);
    bool macho_directive(std::string_view name, Statement& statement);
    void coff_section_directive(Statement& statement, bool push);
    void macho_section_directive(Statement& statement, bool push);
    SectionId frame_section(bool eh);
    SectionId debug_section(std::string_view name);
    void macro_directive(Statement& statement);
    void repeat(Statement& statement);
    std::optional<unsigned> cfi_register(Cursor& cursor);
    // The current section and subsection; switching to a subsection starts
    // a fragment in it unless it is the section's last one.
    [[nodiscard]] Position here() const { return {as.current_, as.current_subsection()}; }
    void select(Position position);
    // Orders each section's fragments by subsection before layout.
    void merge_subsections();

    // Layout and output.
    void materialize_row();
    bool layout(bool report);
    void emit_frames(bool eh);
    void emit_line_table();
    bool resolve_fixups();
    // Whether a fixup's value is known after layout without a relocation.
    bool resolves_in_place(const Value& value, std::size_t section_index, const Fixup& fixup);
    // Mach-O atoms: of a label (through its base symbol for aliases), and
    // of a position within a section.
    std::optional<SymbolId> atom(const Symbol& symbol);
    std::optional<SymbolId> atom_at(std::size_t section_index, std::uint32_t fragment,
                                    std::uint64_t offset);
    bool record_elf(std::size_t section_index, Fragment& fragment, Fixup& fixup, FixupInfo& info,
                    Value& value, std::span<std::uint8_t> bytes, std::uint32_t sequence);
    bool record_coff(std::size_t section_index, Fragment& fragment, Fixup& fixup, FixupInfo& info,
                     Value& value, std::span<std::uint8_t> bytes);
    bool apply(const Fixup& fixup, const FixupInfo& info, std::int64_t number,
               std::span<std::uint8_t> bytes, bool relocated);
    std::vector<std::uint8_t> write();
    std::vector<std::uint8_t> write_elf_object();

    [[nodiscard]] std::uint64_t address(const Symbol& symbol) const {
        const auto& section = as.sections_[symbol.section->value];
        return section.fragments[symbol.fragment].address + symbol.offset;
    }
};

namespace {

// Appends a fragment to the section's current subsection.
Fragment& append_fragment(Section& section, Fragment fragment = {}) {
    fragment.subsection = section.fragments.back().subsection;
    section.fragments.push_back(std::move(fragment));
    return section.fragments.back();
}

} // namespace

// ---------------------------------------------------------------------------
// Assembler basics

Assembler::Assembler(Target& target, const Options& options,
                     SourceManager& sources, Diagnostics& diagnostics)
    : target_(target), options_(options), sources_(sources),
      diagnostics_(diagnostics), object_(target.object()),
      impl_(std::make_unique<Impl>(*this)) {
    SectionId text;
    switch (object_.format) {
    case Format::Elf:
        // The initial .text is code-aligned to 4, as in LLVM MC.
        text = section(".text", elf::sht_progbits, elf::shf_alloc | elf::shf_execinstr);
        sections_[text.value].alignment = 4;
        break;
    case Format::Coff:
        // As LLVM MC, every object has .text, .data, and .bss, aligned to 4.
        text = coff_section(".text", coff::scn_cnt_code | coff::scn_mem_execute |
                                         coff::scn_mem_read);
        sections_[text.value].alignment = 4;
        sections_[coff_section(".data", coff::scn_cnt_initialized_data |
                                            coff::scn_mem_read | coff::scn_mem_write)
                      .value].alignment = 4;
        sections_[coff_section(".bss", coff::scn_cnt_uninitialized_data |
                                           coff::scn_mem_read | coff::scn_mem_write)
                      .value].alignment = 4;
        break;
    case Format::MachO:
        text = macho_section("__TEXT", "__text", macho::s_attr_pure_instructions);
        break;
    }
    switch_section(text);
    impl_->previous = {text, 0};
}

Assembler::~Assembler() = default;

void Assembler::error(SourceLocation location, std::string_view message) {
    diagnostics_.error(location, message);
}

void Assembler::warning(SourceLocation location, std::string_view message) {
    if (options_.no_warnings) return;
    if (options_.fatal_warnings) {
        diagnostics_.error(location, message);
    } else {
        diagnostics_.warning(location, message);
    }
}

void Assembler::write_integer(std::span<std::uint8_t> bytes,
                              std::uint64_t value) const {
    const auto size = bytes.size();
    for (std::size_t index = 0; index < size; ++index) {
        const auto shift = object_.big_endian ? (size - 1 - index) * 8 : index * 8;
        bytes[index] = static_cast<std::uint8_t>(value >> shift);
    }
}

std::uint64_t Assembler::read_integer(std::span<const std::uint8_t> bytes) const {
    std::uint64_t value = 0;
    const auto size = bytes.size();
    for (std::size_t index = 0; index < size; ++index) {
        const auto shift = object_.big_endian ? (size - 1 - index) * 8 : index * 8;
        value |= std::uint64_t{bytes[index]} << shift;
    }
    return value;
}

SymbolId Assembler::symbol(std::string_view name, SourceLocation location) {
    const auto found = impl_->symbol_map.find(std::string(name));
    if (found != impl_->symbol_map.end()) return found->second;
    const SymbolId id{static_cast<std::uint32_t>(symbols_.size())};
    Symbol entry;
    entry.name = std::string(name);
    entry.temporary = (object_.format == Format::MachO ? name.starts_with("L")
                                                       : name.starts_with(".L")) ||
                      target_.private_label(name);
    entry.location = location;
    symbols_.push_back(std::move(entry));
    impl_->resolving.push_back(false);
    impl_->symbol_map.emplace(std::string(name), id);
    return id;
}

SymbolId Assembler::temporary_label() {
    const SymbolId id{static_cast<std::uint32_t>(symbols_.size())};
    Symbol entry;
    entry.temporary = true;
    symbols_.push_back(std::move(entry));
    impl_->resolving.push_back(false);
    define_label(id, {});
    return id;
}

void Assembler::define_label(SymbolId id, SourceLocation location) {
    auto& entry = symbol(id);
    if (entry.defined()) {
        error(location, "symbol '" + entry.name + "' is already defined");
        return;
    }
    auto& current = section(current_);
    if (current.fragments.empty() ||
        current.fragments.back().kind != Fragment::Kind::Data ||
        (object_.format == Format::MachO && !entry.temporary &&
         !current.fragments.back().bytes.empty())) {
        append_fragment(current);
    }
    entry.section = current_;
    entry.fragment = static_cast<std::uint32_t>(current.fragments.size() - 1);
    entry.offset = current.fragments.back().bytes.size();
    entry.definition = ++impl_->definitions;
    if (location.valid()) entry.location = location;
}

void Assembler::add_file_symbol(std::string name) {
    impl_->file_symbols.push_back(std::move(name));
    impl_->file_symbol_order.push_back(static_cast<std::uint32_t>(symbols_.size()));
}

void Assembler::move_label(SymbolId id) {
    auto& current = section(current_);
    if (current.fragments.back().kind != Fragment::Kind::Data) append_fragment(current);
    auto& entry = symbol(id);
    entry.section = current_;
    entry.fragment = static_cast<std::uint32_t>(current.fragments.size() - 1);
    entry.offset = current.fragments.back().bytes.size();
}

void Assembler::assign(SymbolId id, ExpressionId value,
                       SourceLocation location) {
    auto& entry = symbol(id);
    if (entry.section || entry.common) {
        error(location, "symbol '" + entry.name + "' is already defined");
        return;
    }
    // A value that is absolute now is captured, so a later reassignment
    // such as `.set n, n + 1` sees this one.
    if (const auto known = absolute_now(value)) {
        entry.variable = constant(*known, location);
    } else {
        entry.variable = value;
    }
    if (location.valid()) entry.location = location;
}

void Assembler::assignment_statement(Statement& statement) {
    auto& cursor = statement.operands;
    const auto name = cursor.symbol_name();
    if (!name || !cursor.consume(",")) {
        error(cursor.location(), "expected 'symbol, expression'");
        return;
    }
    const auto value = parse_expression(cursor);
    if (!value) return;
    if (!cursor.at_end()) {
        error(cursor.location(), "unexpected text after expression");
        return;
    }
    assign(symbol(*name, statement.location), *value, statement.location);
}

namespace {

using SectionKey = std::tuple<std::string, std::uint32_t, std::uint64_t>;

} // namespace

SectionId Assembler::section(std::string_view name, std::uint32_t type,
                             std::uint64_t flags, std::uint64_t entry_size,
                             std::optional<SymbolId> group, bool comdat,
                             std::uint64_t unique) {
    const SectionKey key{std::string(name), group ? group->value + 1 : 0U, unique};
    if (const auto found = impl_->section_map.find(key);
        found != impl_->section_map.end()) {
        return found->second;
    }
    Section entry;
    entry.name = std::string(name);
    entry.type = target_.section_type(name, type);
    entry.flags = target_.section_flags(name, flags) | (group ? elf::shf_group : 0);
    entry.entry_size = entry_size;
    entry.group = group;
    entry.comdat = comdat;
    entry.unique = unique;
    entry.code = (flags & elf::shf_execinstr) != 0;
    entry.zero_fill = entry.type == elf::sht_nobits;
    return impl_->add_section(std::move(entry), key);
}

SectionId Assembler::coff_section(std::string_view name, std::uint32_t characteristics,
                                  std::optional<SymbolId> comdat, std::uint8_t selection) {
    const SectionKey key{std::string(name), comdat ? comdat->value + 1 : 0U, selection};
    if (const auto found = impl_->section_map.find(key);
        found != impl_->section_map.end()) {
        return found->second;
    }
    Section entry;
    entry.name = std::string(name);
    entry.type = (characteristics & coff::scn_cnt_uninitialized_data) != 0 ? elf::sht_nobits
                                                                          : elf::sht_progbits;
    entry.characteristics = characteristics | (comdat ? coff::scn_lnk_comdat : 0);
    entry.group = comdat;
    entry.selection = selection;
    entry.code = (characteristics & coff::scn_cnt_code) != 0;
    entry.zero_fill = (characteristics & coff::scn_cnt_uninitialized_data) != 0;
    return impl_->add_section(std::move(entry), key);
}

SectionId Assembler::macho_section(std::string_view segment, std::string_view name,
                                   std::uint32_t flags) {
    const SectionKey key{std::string(segment) + "," + std::string(name), 0U, 0};
    if (const auto found = impl_->section_map.find(key);
        found != impl_->section_map.end()) {
        return found->second;
    }
    Section entry;
    entry.name = std::string(name);
    entry.segment = std::string(segment);
    entry.characteristics = flags;
    const auto type = flags & macho::section_type;
    entry.zero_fill = type == macho::s_zerofill || type == macho::s_gb_zerofill ||
                      type == macho::s_thread_local_zerofill;
    entry.type = entry.zero_fill ? elf::sht_nobits : elf::sht_progbits;
    entry.code = (flags & (macho::s_attr_pure_instructions | macho::s_attr_some_instructions)) != 0;
    return impl_->add_section(std::move(entry), key);
}

std::int64_t Assembler::current_subsection() const {
    return sections_[current_.value].fragments.back().subsection;
}

void Assembler::Impl::select(Position position) {
    as.current_ = position.section;
    auto& section = as.sections_[position.section.value];
    if (section.fragments.back().subsection == position.subsection) return;
    Fragment fragment;
    fragment.subsection = position.subsection;
    section.fragments.push_back(std::move(fragment));
    has_subsections = true;
}

void Assembler::switch_section(SectionId id, std::int64_t subsection) {
    const auto here = impl_->here();
    const Impl::Position next{id, subsection};
    if (!(next == here)) impl_->previous = here;
    impl_->select(next);
}

void Assembler::push_section(SectionId id, std::int64_t subsection) {
    impl_->section_stack.emplace_back(impl_->here(), impl_->previous);
    switch_section(id, subsection);
}

void Assembler::pop_section(SourceLocation location) {
    if (impl_->section_stack.empty()) {
        error(location, ".popsection without a matching .pushsection");
        return;
    }
    impl_->select(impl_->section_stack.back().first);
    impl_->previous = impl_->section_stack.back().second;
    impl_->section_stack.pop_back();
}

void Assembler::emit_bytes(std::span<const std::uint8_t> bytes) {
    impl_->materialize_row();
    emit_padding(bytes);
}

void Assembler::emit_padding(std::span<const std::uint8_t> bytes) {
    auto& current = section(current_);
    if (current.fragments.back().kind != Fragment::Kind::Data) {
        append_fragment(current);
    }
    auto& data = current.fragments.back().bytes;
    data.insert(data.end(), bytes.begin(), bytes.end());
}

void Assembler::emit_integer(std::uint64_t value, unsigned size) {
    std::array<std::uint8_t, 8> bytes{};
    write_integer(std::span(bytes.data(), size), value);
    emit_bytes(std::span<const std::uint8_t>(bytes.data(), size));
}

void Assembler::add_fixup(FixupKind kind, ExpressionId expression,
                          SourceLocation location) {
    impl_->materialize_row();
    auto& current = section(current_);
    if (current.fragments.back().kind != Fragment::Kind::Data) {
        append_fragment(current);
    }
    auto& fragment = current.fragments.back();
    fragment.fixups.push_back(
        {static_cast<std::uint32_t>(current.fragments.size() - 1),
         static_cast<std::uint32_t>(fragment.bytes.size()), kind, expression,
         location});
}

void Assembler::emit_value(ExpressionId expression, unsigned size,
                           SourceLocation location) {
    if (const auto value = absolute_now(expression)) {
        if (!fits(*value, size)) {
            error(location, "value " + std::to_string(*value) +
                                " does not fit in " + std::to_string(size) +
                                (size == 1 ? " byte" : " bytes"));
        }
        emit_integer(static_cast<std::uint64_t>(*value), size);
        return;
    }
    add_fixup(FixupKind{static_cast<std::uint16_t>(size)}, expression, location);
    const std::array<std::uint8_t, 8> zero{};
    emit_bytes(std::span(zero.data(), size));
}

void Assembler::emit_alignment(std::uint64_t alignment,
                               std::optional<std::uint8_t> fill,
                               std::uint64_t max_skip) {
    auto& current = section(current_);
    current.alignment = std::max(current.alignment, alignment);
    Fragment fragment;
    fragment.kind = Fragment::Kind::Align;
    fragment.alignment = alignment;
    fragment.max_skip = max_skip;
    fragment.fill = fill.value_or(0);
    fragment.code_fill = !fill && current.code;
    append_fragment(current, std::move(fragment));
}

void Assembler::emit_relaxable(std::vector<std::uint8_t> bytes, std::uint32_t offset,
                               FixupKind kind, std::vector<std::uint8_t> relaxed,
                               std::uint32_t relaxed_offset, FixupKind relaxed_kind,
                               ExpressionId expression, SourceLocation location) {
    impl_->materialize_row();
    auto& current = section(current_);
    Fragment fragment;
    fragment.kind = Fragment::Kind::Relaxable;
    const auto index = static_cast<std::uint32_t>(current.fragments.size());
    fragment.bytes = std::move(bytes);
    fragment.fixups.push_back({index, offset, kind, expression, location});
    fragment.relaxed = std::move(relaxed);
    fragment.relaxed_fixup = {index, relaxed_offset, relaxed_kind, expression, location};
    fragment.location = location;
    append_fragment(current, std::move(fragment));
}

void Assembler::begin_instruction() {
    impl_->materialize_row();
    sections_[current_.value].has_instructions = true;
}

std::uint64_t Assembler::section_address(SectionId id) const {
    return id.value < impl_->section_addresses.size() ? impl_->section_addresses[id.value] : 0;
}

std::uint64_t Assembler::address(const Symbol& symbol) const {
    return impl_->address(symbol);
}

const Symbol* Assembler::base_symbol(const Symbol& symbol) {
    if (symbol.section || symbol.common) return &symbol;
    if (!symbol.variable) return nullptr;
    const auto value = evaluate(*symbol.variable, false);
    if (!value || !value->add || value->subtract) return nullptr;
    const auto& base = symbols_[value->add->value];
    return base.section ? &base : nullptr;
}

std::optional<SymbolId> Assembler::atom(SymbolId id) const {
    return impl_->atom(symbols_[id.value]);
}

// ---------------------------------------------------------------------------
// Expressions

ExpressionId Assembler::constant(std::int64_t value, SourceLocation location) {
    Expression expression;
    expression.kind = Expression::Kind::Constant;
    expression.value = value;
    expression.location = location;
    expressions_.push_back(expression);
    return {static_cast<std::uint32_t>(expressions_.size() - 1)};
}

ExpressionId Assembler::symbol_reference(SymbolId id, SourceLocation location,
                                         std::uint8_t specifier) {
    Expression expression;
    expression.kind = Expression::Kind::Symbol;
    expression.symbol = id;
    expression.specifier = specifier;
    expression.location = location;
    expressions_.push_back(expression);
    return {static_cast<std::uint32_t>(expressions_.size() - 1)};
}

ExpressionId Assembler::binary(Operator op, ExpressionId left,
                               ExpressionId right) {
    Expression expression;
    expression.kind = Expression::Kind::Binary;
    expression.op = op;
    expression.left = left;
    expression.right = right;
    expression.location = expressions_[left.value].location;
    expressions_.push_back(expression);
    return {static_cast<std::uint32_t>(expressions_.size() - 1)};
}

namespace {

struct BinaryOperator {
    std::string_view spelling;
    Operator op;
    int precedence;
};

// GNU as precedence; longer spellings first.
constexpr std::array binary_operators{
    BinaryOperator{"||", Operator::LogicalOr, 1},
    BinaryOperator{"&&", Operator::LogicalAnd, 2},
    BinaryOperator{"==", Operator::Equal, 3},
    BinaryOperator{"!=", Operator::NotEqual, 3},
    BinaryOperator{"<>", Operator::NotEqual, 3},
    BinaryOperator{"<=", Operator::LessEqual, 3},
    BinaryOperator{">=", Operator::GreaterEqual, 3},
    BinaryOperator{"<<", Operator::ShiftLeft, 6},
    BinaryOperator{">>", Operator::ShiftRight, 6},
    BinaryOperator{"<", Operator::Less, 3},
    BinaryOperator{">", Operator::Greater, 3},
    BinaryOperator{"+", Operator::Add, 4},
    BinaryOperator{"-", Operator::Subtract, 4},
    BinaryOperator{"|", Operator::Or, 5},
    BinaryOperator{"!", Operator::OrNot, 5},
    BinaryOperator{"^", Operator::Xor, 5},
    BinaryOperator{"&", Operator::And, 5},
    BinaryOperator{"*", Operator::Multiply, 6},
    BinaryOperator{"/", Operator::Divide, 6},
    BinaryOperator{"%", Operator::Remainder, 6},
};

} // namespace

std::optional<ExpressionId> Assembler::parse_expression(Cursor& cursor) {
    std::function<std::optional<ExpressionId>(int)> parse_binary;
    std::function<std::optional<ExpressionId>()> parse_unary;
    const auto primary = [&]() -> std::optional<ExpressionId> {
        cursor.skip_space();
        const auto location = cursor.location();
        const auto rest = cursor.rest();
        if (rest.empty()) {
            error(location, "expected an expression");
            return std::nullopt;
        }
        const char c = rest.front();
        if (c == '(') {
            cursor.consume("(");
            auto inner = parse_binary(1);
            if (!inner) return std::nullopt;
            if (!cursor.consume(")")) {
                error(cursor.location(), "expected ')'");
                return std::nullopt;
            }
            return inner;
        }
        if (c == '\'') {
            std::size_t index = 1;
            if (index >= rest.size()) {
                error(location, "unterminated character constant");
                return std::nullopt;
            }
            std::int64_t value = 0;
            if (rest[index] == '\\' && index + 1 < rest.size()) {
                const char escaped = rest[index + 1];
                index += 2;
                switch (escaped) {
                case 'n': value = '\n'; break;
                case 't': value = '\t'; break;
                case 'r': value = '\r'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'v': value = '\v'; break;
                case 'a': value = '\a'; break;
                case '0': value = 0; break;
                default: value = static_cast<unsigned char>(escaped); break;
                }
            } else {
                value = static_cast<unsigned char>(rest[index]);
                ++index;
            }
            if (index < rest.size() && rest[index] == '\'') ++index;
            cursor.set_position(cursor.position() + index);
            return constant(value, location);
        }
        if (digit(c)) {
            std::size_t end = 0;
            while (end < rest.size() && identifier_char(rest[end])) ++end;
            const auto token = rest.substr(0, end);
            // Numeric local label references: 1b, 1f.
            if (token.size() >= 2 &&
                (token.back() == 'b' || token.back() == 'f' ||
                 token.back() == 'B' || token.back() == 'F') &&
                std::all_of(token.begin(), token.end() - 1, digit) &&
                !(token.size() == 2 && token[0] == '0' &&
                  (token[1] == 'b' || token[1] == 'B'))) {
                std::uint64_t number = 0;
                std::from_chars(token.data(), token.data() + token.size() - 1,
                                number);
                const bool forward = token.back() == 'f' || token.back() == 'F';
                if (!forward && impl_->local_label_instances[number] == 0) {
                    error(location, "no previous definition of local label '" +
                                        std::to_string(number) + "'");
                    cursor.set_position(cursor.position() + end);
                    return std::nullopt;
                }
                cursor.set_position(cursor.position() + end);
                return symbol_reference(
                    symbol(impl_->numeric_label_name(number, forward), location),
                    location);
            }
            int base = 10;
            std::string_view digits = token;
            if (token.size() > 2 && token[0] == '0' &&
                (token[1] == 'x' || token[1] == 'X')) {
                base = 16;
                digits = token.substr(2);
            } else if (token.size() > 2 && token[0] == '0' &&
                       (token[1] == 'b' || token[1] == 'B')) {
                base = 2;
                digits = token.substr(2);
            } else if (token.size() > 1 && token[0] == '0') {
                base = 8;
                digits = token.substr(1);
            }
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(
                digits.data(), digits.data() + digits.size(), value, base);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != digits.data() + digits.size()) {
                error(location, parsed.ec == std::errc::result_out_of_range
                                    ? "integer constant does not fit in 64 bits"
                                    : "invalid integer constant '" +
                                          std::string(token) + "'");
                cursor.set_position(cursor.position() + end);
                return std::nullopt;
            }
            cursor.set_position(cursor.position() + end);
            return constant(static_cast<std::int64_t>(value), location);
        }
        if (c == '.' && (rest.size() == 1 || !identifier_char(rest[1]))) {
            cursor.set_position(cursor.position() + 1);
            const auto dot = temporary_label();
            target_.label(*this, dot);
            return symbol_reference(dot, location);
        }
        if (identifier_start(c) || c == '"') {
            const auto name = cursor.symbol_name();
            if (!name) {
                error(location, "unterminated symbol name");
                return std::nullopt;
            }
            std::uint8_t specifier = 0;
            if (cursor.rest().starts_with("@")) {
                // A relocation specifier: `sym@PLT`.
                auto probe = cursor;
                probe.set_position(probe.position() + 1);
                const auto spelling_location = probe.location();
                const auto spelling = probe.identifier();
                if (spelling) {
                    const auto number = target_.specifier(*spelling);
                    if (!number) {
                        error(spelling_location, "unknown relocation specifier '@" +
                                                     std::string(*spelling) + "'");
                        return std::nullopt;
                    }
                    specifier = *number;
                    cursor = probe;
                }
            }
            return symbol_reference(symbol(*name, location), location, specifier);
        }
        error(location, std::string("unexpected '") + c + "' in expression");
        return std::nullopt;
    };
    parse_unary = [&]() -> std::optional<ExpressionId> {
        cursor.skip_space();
        const auto location = cursor.location();
        const auto rest = cursor.rest();
        if (!rest.empty() && (rest[0] == '-' || rest[0] == '~' ||
                              (rest[0] == '!' && !rest.starts_with("!=")) ||
                              rest[0] == '+')) {
            const char c = rest[0];
            cursor.set_position(cursor.position() + 1);
            auto operand = parse_unary();
            if (!operand) return std::nullopt;
            if (c == '+') return operand;
            Expression expression;
            expression.kind = Expression::Kind::Unary;
            expression.op = c == '-'   ? Operator::Negate
                            : c == '~' ? Operator::Complement
                                       : Operator::LogicalNot;
            expression.left = *operand;
            expression.location = location;
            expressions_.push_back(expression);
            return ExpressionId{static_cast<std::uint32_t>(expressions_.size() - 1)};
        }
        return primary();
    };
    parse_binary = [&](int minimum) -> std::optional<ExpressionId> {
        auto left = parse_unary();
        if (!left) return std::nullopt;
        for (;;) {
            cursor.skip_space();
            const auto rest = cursor.rest();
            const BinaryOperator* match = nullptr;
            for (const auto& candidate : binary_operators) {
                if (rest.starts_with(candidate.spelling)) {
                    match = &candidate;
                    break;
                }
            }
            if (!match || match->precedence < minimum) return left;
            cursor.set_position(cursor.position() + match->spelling.size());
            auto right = parse_binary(match->precedence + 1);
            if (!right) return std::nullopt;
            left = binary(match->op, *left, *right);
        }
    };
    return parse_binary(1);
}

std::optional<std::int64_t> Assembler::absolute_now(ExpressionId id) {
    const auto value = evaluate(id, false);
    if (!value || !value->absolute()) return std::nullopt;
    return value->constant;
}

std::optional<std::int64_t> Assembler::constant_now(ExpressionId id) {
    const std::function<bool(ExpressionId)> label_free = [&](ExpressionId each) {
        const auto& expression = expressions_[each.value];
        switch (expression.kind) {
        case Expression::Kind::Constant: return true;
        case Expression::Kind::Symbol: {
            const auto& entry = symbols_[expression.symbol.value];
            if (expression.specifier != 0 || !entry.variable ||
                entry.binding == Symbol::Binding::Weak || impl_->resolving[expression.symbol.value]) {
                return false;
            }
            impl_->resolving[expression.symbol.value] = true;
            const bool free = label_free(*entry.variable);
            impl_->resolving[expression.symbol.value] = false;
            return free;
        }
        case Expression::Kind::Unary: return label_free(expression.left);
        case Expression::Kind::Binary:
            return label_free(expression.left) && label_free(expression.right);
        }
        return false;
    };
    if (!label_free(id)) return std::nullopt;
    return absolute_now(id);
}

std::optional<std::int64_t> Assembler::parse_absolute(Cursor& cursor) {
    const auto location = cursor.location();
    const auto expression = parse_expression(cursor);
    if (!expression) return std::nullopt;
    const auto value = evaluate(*expression, true);
    if (!value) return std::nullopt;
    if (!value->absolute()) {
        error(location, "expression must be absolute here");
        return std::nullopt;
    }
    return value->constant;
}

std::optional<Value> Assembler::evaluate(ExpressionId id, bool report, bool keep_aliases,
                                         bool in_set) {
    const auto& expression = expressions_[id.value];
    const auto fail = [&](std::string_view message) -> std::optional<Value> {
        if (report) error(expression.location, message);
        return std::nullopt;
    };
    // Folds `a - b` once both labels have positions relative to each other.
    const auto fold = [&](Value value) -> Value {
        if (!value.add || !value.subtract) return value;
        if (*value.add == *value.subtract) {
            value.add.reset();
            value.subtract.reset();
            return value;
        }
        const auto& left = symbols_[value.add->value];
        const auto& right = symbols_[value.subtract->value];
        if (!left.section || !right.section || !(*left.section == *right.section)) {
            return value;
        }
        // A COFF function's address is always relocated; a Mach-O label may
        // move with its atom.
        if (object_.format == Format::Coff && (left.coff_type >> 4U) == 2) return value;
        if (object_.format == Format::MachO && !in_set &&
            !(atom(*value.add) == atom(*value.subtract))) {
            return value;
        }
        if (impl_->layout_known || left.fragment == right.fragment) {
            const auto offset = impl_->layout_known
                ? static_cast<std::int64_t>(impl_->address(left)) -
                      static_cast<std::int64_t>(impl_->address(right))
                : static_cast<std::int64_t>(left.offset) -
                      static_cast<std::int64_t>(right.offset);
            value.constant += offset;
            value.add.reset();
            value.subtract.reset();
        }
        return value;
    };
    switch (expression.kind) {
    case Expression::Kind::Constant:
        return Value{std::nullopt, std::nullopt, expression.value};
    case Expression::Kind::Symbol: {
        const auto& entry = symbols_[expression.symbol.value];
        if (entry.variable && entry.binding != Symbol::Binding::Weak) {
            if (impl_->resolving[expression.symbol.value]) {
                return fail("symbol '" + entry.name + "' is defined in terms of itself");
            }
            impl_->resolving[expression.symbol.value] = true;
            auto value = evaluate(*entry.variable, report, keep_aliases, true);
            impl_->resolving[expression.symbol.value] = false;
            if (keep_aliases && value && !value->absolute() &&
                entry.binding == Symbol::Binding::Global) {
                return Value{expression.symbol, std::nullopt, 0, expression.specifier};
            }
            if (value && expression.specifier != 0) value->specifier = expression.specifier;
            return value;
        }
        return Value{expression.symbol, std::nullopt, 0, expression.specifier};
    }
    case Expression::Kind::Unary: {
        auto operand = evaluate(expression.left, report, keep_aliases, in_set);
        if (!operand) return std::nullopt;
        if (expression.op == Operator::Negate && operand->add &&
            operand->subtract) {
            std::swap(operand->add, operand->subtract);
            operand->constant = -operand->constant;
            return operand;
        }
        if (!operand->absolute()) {
            return fail("operand of a unary operator must be absolute");
        }
        const auto v = operand->constant;
        switch (expression.op) {
        case Operator::Negate:
            return Value{std::nullopt, std::nullopt,
                         static_cast<std::int64_t>(0ULL - static_cast<std::uint64_t>(v))};
        case Operator::Complement: return Value{std::nullopt, std::nullopt, ~v};
        default: return Value{std::nullopt, std::nullopt, v == 0 ? 1 : 0};
        }
    }
    case Expression::Kind::Binary: {
        auto left = evaluate(expression.left, report, keep_aliases, in_set);
        if (!left) return std::nullopt;
        auto right = evaluate(expression.right, report, keep_aliases, in_set);
        if (!right) return std::nullopt;
        if (expression.op == Operator::Add || expression.op == Operator::Subtract) {
            if (expression.op == Operator::Subtract) {
                std::swap(right->add, right->subtract);
                right->constant = static_cast<std::int64_t>(
                    0ULL - static_cast<std::uint64_t>(right->constant));
            }
            Value result;
            result.constant = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(left->constant) +
                static_cast<std::uint64_t>(right->constant));
            if (left->add && right->add) {
                return fail("expression adds two symbols");
            }
            if (left->subtract && right->subtract) {
                return fail("expression subtracts two symbols");
            }
            result.add = left->add ? left->add : right->add;
            result.subtract = left->subtract ? left->subtract : right->subtract;
            result.specifier = left->add ? left->specifier : right->specifier;
            return fold(result);
        }
        *left = fold(*left);
        *right = fold(*right);
        if (!left->absolute() || !right->absolute()) {
            return fail("operands of this operator must be absolute");
        }
        const auto a = left->constant;
        const auto b = right->constant;
        const auto ua = static_cast<std::uint64_t>(a);
        const auto ub = static_cast<std::uint64_t>(b);
        std::int64_t r = 0;
        switch (expression.op) {
        case Operator::Multiply: r = static_cast<std::int64_t>(ua * ub); break;
        case Operator::Divide:
        case Operator::Remainder:
            if (b == 0) return fail("division by zero");
            if (a == std::numeric_limits<std::int64_t>::min() && b == -1) {
                r = expression.op == Operator::Divide ? a : 0;
            } else {
                r = expression.op == Operator::Divide ? a / b : a % b;
            }
            break;
        case Operator::ShiftLeft: r = ub >= 64 ? 0 : static_cast<std::int64_t>(ua << ub); break;
        case Operator::ShiftRight: r = ub >= 64 ? 0 : static_cast<std::int64_t>(ua >> ub); break;
        case Operator::And: r = a & b; break;
        case Operator::Or: r = a | b; break;
        case Operator::Xor: r = a ^ b; break;
        case Operator::OrNot: r = a | ~b; break;
        case Operator::Equal: r = a == b ? -1 : 0; break;
        case Operator::NotEqual: r = a != b ? -1 : 0; break;
        case Operator::Less: r = a < b ? -1 : 0; break;
        case Operator::LessEqual: r = a <= b ? -1 : 0; break;
        case Operator::Greater: r = a > b ? -1 : 0; break;
        case Operator::GreaterEqual: r = a >= b ? -1 : 0; break;
        case Operator::LogicalAnd: r = (a != 0 && b != 0) ? 1 : 0; break;
        case Operator::LogicalOr: r = (a != 0 || b != 0) ? 1 : 0; break;
        default: break;
        }
        return Value{std::nullopt, std::nullopt, r};
    }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Statements

std::string Assembler::Impl::numeric_label_name(std::uint64_t number,
                                                bool forward) {
    const auto instance = local_label_instances[number] + (forward ? 1 : 0);
    return ".L" + std::to_string(number) + "\x02" + std::to_string(instance);
}

void Assembler::Impl::define_numeric_label(std::uint64_t number,
                                           SourceLocation location) {
    ++local_label_instances[number];
    const auto id = as.symbol(numeric_label_name(number, false), location);
    as.define_label(id, location);
    as.target_.label(as, id);
}

void Assembler::Impl::process_text(std::string_view text, const SourceFile* file,
                                   std::size_t base_offset, unsigned first_line,
                                   std::optional<SourceLocation> origin) {
    std::size_t start = 0;
    unsigned line = first_line;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        SourceLocation location;
        bool exact = true;
        if (origin) {
            location = *origin;
            exact = false;
        } else {
            location.file = file;
            location.offset = base_offset + start;
            location.line = line;
            location.column = 1;
        }
        process_line(text.substr(start, end - start), location, exact);
        if (exit_macro && origin) return;
        if (end == text.size()) break;
        start = end + 1;
        ++line;
    }
}

void Assembler::Impl::process_line(std::string_view line, SourceLocation location,
                                   bool exact) {
    if (capture) {
        // Macro and repeat bodies are kept as text; only nesting counts.
        Cursor cursor(line, location);
        auto probe = cursor;
        std::string first;
        if (const auto name = probe.identifier()) first = lower(*name);
        if (probe.consume(":")) {
            if (const auto name = probe.identifier()) first = lower(*name);
        }
        const bool opens = first == (capture->kind == Capture::Kind::Macro ? ".macro" : ".rept");
        const bool closes = capture->kind == Capture::Kind::Macro
                                ? (first == ".endm" || first == ".endmacro")
                                : first == ".endr";
        if (opens) ++capture->depth;
        if (closes) {
            if (capture->depth == 0) {
                auto finished = std::move(*capture);
                capture.reset();
                if (finished.kind == Capture::Kind::Macro) {
                    finished.macro.body.shrink_to_fit();
                    const auto name = finished.macro.name;
                    macros[name] = std::move(finished.macro);
                } else {
                    std::string body;
                    for (const auto& text : finished.macro.body) {
                        body += text;
                        body += '\n';
                    }
                    if (!body.empty()) body.pop_back();
                    if (++expansion_depth > 64) {
                        as.error(finished.location, "repetitions nest too deeply");
                    } else {
                        for (std::uint64_t index = 0; index < finished.count; ++index) {
                            process_text(body, nullptr, 0, 1, finished.location);
                        }
                    }
                    --expansion_depth;
                }
                return;
            }
            --capture->depth;
        }
        capture->macro.body.emplace_back(line);
        return;
    }

    // Blank comments while keeping columns: '#' to the end of the line and
    // /* ... */ blocks, outside strings and character constants.
    std::string text(line);
    bool in_string = false;
    std::vector<std::size_t> separators;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (in_block_comment) {
            if (text.compare(index, 2, "*/") == 0) {
                text[index] = ' ';
                text[index + 1] = ' ';
                ++index;
                in_block_comment = false;
            } else {
                text[index] = ' ';
            }
            continue;
        }
        const char c = text[index];
        if (in_string) {
            if (c == '\\') ++index;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '\'') {
            if (index + 1 < text.size() && text[index + 1] == '\\') index += 2;
            else ++index;
            if (index + 1 < text.size() && text[index + 1] == '\'') ++index;
        } else if (c == '#') {
            text.resize(index);
            break;
        } else if (c == '/' && index + 1 < text.size() && text[index + 1] == '*') {
            in_block_comment = true;
            text[index] = ' ';
            text[index + 1] = ' ';
            ++index;
        } else if (c == ';') {
            separators.push_back(index);
        }
    }
    std::size_t begin = 0;
    separators.push_back(text.size());
    for (const auto separator : separators) {
        auto part_location = location;
        if (exact) {
            part_location.offset += begin;
            part_location.column += static_cast<unsigned>(begin);
        }
        statement(std::string_view(text).substr(begin, separator - begin),
                  part_location, exact);
        if (exit_macro) return;
        begin = separator + 1;
    }
}

void Assembler::Impl::statement(std::string_view text, SourceLocation location,
                                bool exact) {
    Cursor cursor(text, location, exact);
    const bool active = conditionals.empty() || conditionals.back().active;

    // Labels.
    for (;;) {
        cursor.skip_space();
        if (cursor.at_end()) return;
        const auto save = cursor.position();
        const auto label_location = cursor.location();
        if (digit(cursor.peek())) {
            const auto rest = cursor.rest();
            std::size_t end = 0;
            while (end < rest.size() && digit(rest[end])) ++end;
            if (end < rest.size() && rest[end] == ':') {
                cursor.set_position(save + end + 1);
                if (!active) continue;
                std::uint64_t number = 0;
                std::from_chars(rest.data(), rest.data() + end, number);
                define_numeric_label(number, label_location);
                continue;
            }
            break;
        }
        const auto name = cursor.symbol_name();
        if (name && cursor.rest().starts_with(":") &&
            !cursor.rest().starts_with("::")) {
            cursor.set_position(cursor.position() + 1);
            if (active) {
                const auto id = as.symbol(*name, label_location);
                // As in LLVM MC: a Mach-O label the linker sees starts an
                // atom, which a frame may not span.
                const auto& entry = as.symbol(id);
                if (as.object_.format == Format::MachO && open_frame &&
                    (entry.binding == Symbol::Binding::Global ||
                     entry.binding == Symbol::Binding::Weak) &&
                    !entry.alt_entry) {
                    as.error(label_location,
                             "non-private labels cannot appear between "
                             ".cfi_startproc / .cfi_endproc pairs");
                }
                as.define_label(id, label_location);
                as.target_.label(as, id);
            }
            continue;
        }
        cursor.set_position(save);
        break;
    }

    cursor.skip_space();
    const auto name_location = cursor.location();
    auto name = cursor.symbol_name();
    if (!name) {
        if (active) as.error(name_location, "expected a statement");
        return;
    }
    // `symbol = expression`
    if (cursor.peek() == '=' && !cursor.rest().starts_with("==")) {
        cursor.consume("=");
        if (!active) return;
        const auto value = as.parse_expression(cursor);
        if (!value) return;
        if (!cursor.at_end()) {
            as.error(cursor.location(), "unexpected text after expression");
            return;
        }
        as.assign(as.symbol(*name, name_location), *value, name_location);
        return;
    }
    Statement current{*name, cursor, name_location};
    // Instruction mnemonics keep their spelling; directives and conditionals
    // compare case-insensitively, as GNU as does.
    if (conditional_directive(lower(*name), current)) return;
    if (!active) return;
    if (const auto found = macros.find(*name); found != macros.end()) {
        expand_macro(found->second, current);
        return;
    }
    if (name->starts_with(".")) {
        directive(current);
        return;
    }
    as.target_.instruction(as, current);
}

bool Assembler::Impl::conditional_directive(std::string_view name,
                                            Statement& statement) {
    auto& cursor = statement.operands;
    const bool parent = conditionals.empty() || conditionals.back().active;
    const auto evaluate_condition = [&]() -> std::optional<bool> {
        if (!parent) return false;
        if (name == ".ifdef" || name == ".ifndef" || name == ".ifnotdef") {
            const auto symbol_name = cursor.symbol_name();
            if (!symbol_name) {
                as.error(cursor.location(), "expected a symbol name");
                return std::nullopt;
            }
            const auto found = symbol_map.find(*symbol_name);
            const bool defined = found != symbol_map.end() &&
                                 (as.symbols_[found->second.value].defined() ||
                                  as.symbols_[found->second.value].binding !=
                                      Symbol::Binding::Unset);
            return name == ".ifdef" ? defined : !defined;
        }
        if (name == ".ifc" || name == ".ifnc") {
            const auto rest = std::string(cursor.rest());
            const auto comma = rest.find(',');
            if (comma == std::string::npos) {
                as.error(cursor.location(), "expected two comma-separated strings");
                return std::nullopt;
            }
            const auto trim = [](std::string text) {
                const auto first = text.find_first_not_of(" \t");
                const auto last = text.find_last_not_of(" \t");
                return first == std::string::npos ? std::string{}
                                                  : text.substr(first, last - first + 1);
            };
            const bool same = trim(rest.substr(0, comma)) == trim(rest.substr(comma + 1));
            return name == ".ifc" ? same : !same;
        }
        const auto value = as.parse_absolute(cursor);
        if (!value) return std::nullopt;
        if (name == ".if" || name == ".ifne" || name == ".elseif") return *value != 0;
        if (name == ".ifeq") return *value == 0;
        if (name == ".ifgt") return *value > 0;
        if (name == ".ifge") return *value >= 0;
        if (name == ".iflt") return *value < 0;
        if (name == ".ifle") return *value <= 0;
        return false;
    };
    if (name == ".if" || name == ".ifdef" || name == ".ifndef" ||
        name == ".ifnotdef" || name == ".ifeq" || name == ".ifne" ||
        name == ".ifgt" || name == ".ifge" || name == ".iflt" ||
        name == ".ifle" || name == ".ifc" || name == ".ifnc") {
        const auto condition = evaluate_condition();
        const bool value = condition.value_or(false);
        conditionals.push_back({parent && value, value, false});
        return true;
    }
    if (name == ".elseif") {
        if (conditionals.empty()) {
            as.error(statement.location, ".elseif without .if");
            return true;
        }
        auto& top = conditionals.back();
        const bool outer = conditionals.size() < 2 || conditionals[conditionals.size() - 2].active;
        if (top.taken || !outer) {
            top.active = false;
            return true;
        }
        conditionals.pop_back();
        const auto condition = evaluate_condition();
        conditionals.push_back({condition.value_or(false), condition.value_or(false), false});
        return true;
    }
    if (name == ".else") {
        if (conditionals.empty() || conditionals.back().else_seen) {
            as.error(statement.location, ".else without .if");
            return true;
        }
        auto& top = conditionals.back();
        const bool outer = conditionals.size() < 2 || conditionals[conditionals.size() - 2].active;
        top.else_seen = true;
        top.active = outer && !top.taken;
        top.taken = true;
        return true;
    }
    if (name == ".endif") {
        if (conditionals.empty()) {
            as.error(statement.location, ".endif without .if");
            return true;
        }
        conditionals.pop_back();
        return true;
    }
    return false;
}

void Assembler::Impl::expand_macro(const Macro& macro, Statement& statement) {
    auto& cursor = statement.operands;
    std::vector<std::string> arguments(macro.parameters.size());
    std::vector<bool> given(macro.parameters.size());
    // Arguments are separated by commas or, outside parentheses and
    // strings, by blanks; `name=value` selects a parameter by name.
    std::vector<std::string> pieces;
    {
        const auto rest = cursor.rest();
        std::string current;
        int depth = 0;
        bool quoted = false;
        bool pending = false;
        for (std::size_t index = 0; index < rest.size(); ++index) {
            const char c = rest[index];
            if (quoted) {
                current.push_back(c);
                if (c == '\\' && index + 1 < rest.size()) {
                    current.push_back(rest[++index]);
                } else if (c == '"') {
                    quoted = false;
                }
                continue;
            }
            if (c == '"') {
                quoted = true;
                current.push_back(c);
                pending = true;
                continue;
            }
            if (alternate_macro && c == '<' && current.empty()) {
                // `<text>` quotes text; `!` escapes the next character.
                for (++index; index < rest.size() && rest[index] != '>'; ++index) {
                    if (rest[index] == '!' && index + 1 < rest.size()) ++index;
                    current.push_back(rest[index]);
                }
                pending = true;
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (depth == 0 && c == ',') {
                pieces.push_back(current);
                current.clear();
                pending = false;
                continue;
            }
            if (depth == 0 && space(c)) {
                // A blank ends an argument unless an operator joins it.
                std::size_t next = index;
                while (next < rest.size() && space(rest[next])) ++next;
                const bool operator_next = next < rest.size() &&
                    std::string_view("+-*/|&^<>=,").find(rest[next]) != std::string_view::npos;
                const bool operator_before = !current.empty() &&
                    std::string_view("+-*/|&^<>=(").find(current.back()) != std::string_view::npos;
                if (pending && !operator_next && !operator_before && next < rest.size()) {
                    pieces.push_back(current);
                    current.clear();
                    pending = false;
                }
                index = next - 1;
                continue;
            }
            current.push_back(c);
            pending = true;
        }
        if (pending || !pieces.empty()) pieces.push_back(current);
    }
    // An argument's text: in alternate mode `%expr` passes the expression's
    // decimal value; otherwise a quoted argument passes its text, adjacent
    // quoted strings joining and a doubled quote standing for one.
    const auto argument_text = [&](std::string text) -> std::optional<std::string> {
        if (alternate_macro && text.starts_with('%')) {
            Cursor expression(std::string_view(text).substr(1), statement.location, false);
            const auto value = as.parse_absolute(expression);
            if (!value) return std::nullopt;
            return std::to_string(*value);
        }
        if (alternate_macro || !text.starts_with('"')) return text;
        std::string result;
        std::size_t index = 0;
        while (index < text.size() && text[index] == '"') {
            bool escaped = false;
            for (++index; index < text.size(); ++index) {
                const char c = text[index];
                if (!escaped && c == '"') {
                    if (index + 1 < text.size() && text[index + 1] == '"') {
                        result.push_back('"');
                        ++index;
                        continue;
                    }
                    ++index;
                    break;
                }
                escaped = !escaped && c == '\\';
                result.push_back(c);
            }
        }
        result.append(text, index);
        return result;
    };
    std::size_t positional = 0;
    for (auto& piece : pieces) {
        const auto first = piece.find_first_not_of(" \t");
        const auto last = piece.find_last_not_of(" \t");
        piece = first == std::string::npos ? std::string{} : piece.substr(first, last - first + 1);
        const auto equals = piece.find('=');
        if (equals != std::string::npos && equals > 0 &&
            std::all_of(piece.begin(), piece.begin() + static_cast<std::ptrdiff_t>(equals), identifier_char)) {
            const auto key = piece.substr(0, equals);
            const auto found = std::find_if(macro.parameters.begin(), macro.parameters.end(),
                                            [&](const auto& p) { return p.name == key; });
            if (found != macro.parameters.end()) {
                const auto value = argument_text(piece.substr(equals + 1));
                if (!value) return;
                const auto index = static_cast<std::size_t>(found - macro.parameters.begin());
                arguments[index] = *value;
                given[index] = true;
                continue;
            }
        }
        if (auto value = argument_text(piece)) {
            piece = std::move(*value);
        } else {
            return;
        }
        if (positional >= macro.parameters.size()) {
            as.error(statement.location, "too many arguments for macro '" + macro.name + "'");
            return;
        }
        if (macro.parameters[positional].vararg) {
            auto& text = arguments[positional];
            if (given[positional]) text += ',';
            text += piece;
            given[positional] = true;
            continue;
        }
        arguments[positional] = piece;
        given[positional] = true;
        ++positional;
    }
    for (std::size_t index = 0; index < macro.parameters.size(); ++index) {
        if (given[index] && !arguments[index].empty()) continue;
        if (macro.parameters[index].required) {
            as.error(statement.location, "missing value for required parameter '" +
                                             macro.parameters[index].name + "' of macro '" +
                                             macro.name + "'");
            return;
        }
        if (!given[index]) arguments[index] = macro.parameters[index].default_value;
    }
    const auto counter = macro_counter++;
    // `LOCAL name, ...` lines of an alternate-mode expansion name labels
    // unique to it.
    std::vector<std::pair<std::string, std::string>> locals;
    const auto local_line = [&](std::string_view line) {
        Cursor probe(line, statement.location, false);
        const auto word = probe.identifier();
        return alternate_macro && word && lower(*word) == "local" &&
               (probe.at_end() || space(probe.rest().front()));
    };
    for (const auto& line : macro.body) {
        if (!local_line(line)) continue;
        Cursor names(line, statement.location, false);
        (void)names.identifier();
        while (const auto name = names.identifier()) {
            char buffer[24];
            std::snprintf(buffer, sizeof buffer, ".LL%04llx",
                          static_cast<unsigned long long>(++local_counter));
            locals.emplace_back(std::string(*name), buffer);
            names.consume(",");
        }
    }
    const auto substitute = [&](std::string_view name) -> const std::string* {
        for (std::size_t index = 0; index < macro.parameters.size(); ++index) {
            if (macro.parameters[index].name == name) return &arguments[index];
        }
        for (const auto& [local, replacement] : locals) {
            if (local == name) return &replacement;
        }
        return nullptr;
    };
    std::string body;
    for (const auto& line : macro.body) {
        if (local_line(line)) {
            body.push_back('\n');
            continue;
        }
        for (std::size_t index = 0; index < line.size(); ++index) {
            const char c = line[index];
            if (alternate_macro && identifier_start(c) &&
                (index == 0 || !identifier_char(line[index - 1]))) {
                std::size_t end = index;
                while (end < line.size() && identifier_char(line[end])) ++end;
                const auto name = std::string_view(line).substr(index, end - index);
                if (const auto* replacement = substitute(name)) {
                    body += *replacement;
                } else {
                    body += name;
                }
                index = end - 1;
                continue;
            }
            if (c != '\\' || index + 1 >= line.size()) {
                body.push_back(c);
                continue;
            }
            if (line[index + 1] == '@') {
                body += std::to_string(counter);
                ++index;
                continue;
            }
            if (line[index + 1] == '(' && index + 2 < line.size() && line[index + 2] == ')') {
                index += 2;
                continue;
            }
            std::size_t end = index + 1;
            while (end < line.size() && identifier_char(line[end]) && line[end] != '.' &&
                   line[end] != '$') {
                ++end;
            }
            const auto* replacement =
                substitute(std::string_view(line).substr(index + 1, end - index - 1));
            if (!replacement) {
                body.push_back(c);
                continue;
            }
            body += *replacement;
            index = end - 1;
        }
        body.push_back('\n');
    }
    if (!body.empty()) body.pop_back();
    if (++expansion_depth > 64) {
        as.error(statement.location, "macro '" + macro.name + "' expands too deeply");
        --expansion_depth;
        return;
    }
    process_text(body, nullptr, 0, 1, statement.location);
    exit_macro = false;
    --expansion_depth;
}

void Assembler::Impl::macro_directive(Statement& statement) {
    auto& cursor = statement.operands;
    const auto name = cursor.identifier();
    if (!name) {
        as.error(cursor.location(), "expected a macro name");
        return;
    }
    Capture next;
    next.kind = Capture::Kind::Macro;
    next.macro.name = std::string(*name);
    next.macro.location = statement.location;
    next.location = statement.location;
    cursor.consume(",");
    while (!cursor.at_end()) {
        const auto parameter = cursor.identifier();
        if (!parameter) {
            as.error(cursor.location(), "expected a macro parameter name");
            return;
        }
        Macro::Parameter entry;
        entry.name = std::string(*parameter);
        if (cursor.consume(":")) {
            const auto qualifier = cursor.identifier();
            if (qualifier && *qualifier == "req") entry.required = true;
            else if (qualifier && *qualifier == "vararg") entry.vararg = true;
            else {
                as.error(cursor.location(), "unknown macro parameter qualifier");
                return;
            }
        }
        if (cursor.consume("=")) {
            cursor.skip_space();
            const auto rest = cursor.rest();
            std::size_t end = 0;
            if (!rest.empty() && rest[0] == '"') {
                end = rest.find('"', 1);
                end = end == std::string_view::npos ? rest.size() : end + 1;
            } else {
                while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
            }
            entry.default_value = std::string(rest.substr(0, end));
            cursor.set_position(cursor.position() + end);
        }
        next.macro.parameters.push_back(std::move(entry));
        cursor.consume(",");
    }
    capture = std::move(next);
}

void Assembler::Impl::repeat(Statement& statement) {
    const auto count = as.parse_absolute(statement.operands);
    Capture next;
    next.kind = Capture::Kind::Repeat;
    next.count = count && *count > 0 ? static_cast<std::uint64_t>(*count) : 0;
    next.location = statement.location;
    capture = std::move(next);
}

// ---------------------------------------------------------------------------
// Directives

void Assembler::Impl::directive(Statement& statement) {
    if (as.target_.directive(as, statement)) return;
    const auto name = lower(statement.name);
    if (as.object_.format == Format::Coff && coff_directive(name, statement)) return;
    if (as.object_.format == Format::MachO && macho_directive(name, statement)) return;
    auto& cursor = statement.operands;
    const auto expect_end = [&]() {
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after directive");
    };
    using Binding = Symbol::Binding;
    if (name == ".byte") return data(statement, 1);
    if (name == ".2byte" || name == ".short" || name == ".half" ||
        name == ".hword" || name == ".value") return data(statement, 2);
    if (name == ".4byte" || name == ".long" || name == ".int") return data(statement, 4);
    if (name == ".word") return data(statement, as.target_.word_size());
    if (name == ".8byte" || name == ".quad" || name == ".dword") return data(statement, 8);
    if (name == ".uleb128") return leb(statement, false);
    if (name == ".sleb128") return leb(statement, true);
    if (name == ".ascii") return ascii(statement, false);
    if (name == ".asciz" || name == ".string") return ascii(statement, true);
    if (name == ".float" || name == ".single") return floating(statement, 4);
    if (name == ".double") return floating(statement, 8);
    if (name == ".zero") return reserve(statement, true);
    if (name == ".space" || name == ".skip") return reserve(statement, false);
    if (name == ".fill") return fill(statement);
    if (name == ".p2align") return align(statement, true);
    if (name == ".balign") return align(statement, false);
    if (name == ".align") return align(statement, as.target_.power_of_two_align());
    if (name == ".org") return org(statement);
    if (name == ".incbin") return incbin(statement);
    if (name == ".include") return include(statement);
    if (name == ".text" || name == ".data" || name == ".bss") {
        as.target_.flush(as);
        std::int64_t subsection = 0;
        if (!cursor.at_end()) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            subsection = *value;
        }
        expect_end();
        const auto flags = name == ".text" ? elf::shf_alloc | elf::shf_execinstr
                                           : elf::shf_alloc | elf::shf_write;
        as.switch_section(as.section(name, name == ".bss" ? elf::sht_nobits : elf::sht_progbits,
                                     flags),
                          subsection);
        return;
    }
    if (name == ".section") return section_directive(statement, false);
    if (name == ".pushsection") return section_directive(statement, true);
    if (name == ".popsection") {
        as.target_.flush(as);
        expect_end();
        as.pop_section(statement.location);
        return;
    }
    if (name == ".previous") {
        as.target_.flush(as);
        expect_end();
        const auto next = previous;
        previous = here();
        select(next);
        return;
    }
    if (name == ".subsection") {
        as.target_.flush(as);
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        expect_end();
        as.switch_section(as.current_, *value);
        return;
    }
    if (name == ".altmacro" || name == ".noaltmacro") {
        expect_end();
        alternate_macro = name == ".altmacro";
        return;
    }
    if (name == ".globl" || name == ".global") return symbol_binding(statement, Binding::Global);
    if (name == ".local") return symbol_binding(statement, Binding::Local);
    if (name == ".weak") return symbol_binding(statement, Binding::Weak);
    if (name == ".hidden") return symbol_visibility(statement, elf::stv_hidden);
    if (name == ".internal") return symbol_visibility(statement, elf::stv_internal);
    if (name == ".protected") return symbol_visibility(statement, elf::stv_protected);
    if (name == ".type") return type(statement);
    if (name == ".size") return size(statement);
    if (name == ".comm") return common(statement, false);
    if (name == ".lcomm") return common(statement, true);
    if (name == ".set" || name == ".equ" || name == ".equiv") {
        if (name == ".equiv") {
            auto probe = cursor;
            if (const auto symbol_name = probe.symbol_name()) {
                const auto found = symbol_map.find(*symbol_name);
                if (found != symbol_map.end() && as.symbols_[found->second.value].defined()) {
                    as.error(statement.location, "symbol '" + *symbol_name + "' is already defined");
                    return;
                }
            }
        }
        as.assignment_statement(statement);
        return;
    }
    if (name == ".extern") return;
    if (name == ".file") return file(statement);
    if (name == ".loc") return loc(statement);
    if (name.starts_with(".cfi_")) return cfi(statement);
    if (name == ".ident") return ident(statement);
    if (name == ".macro") return macro_directive(statement);
    if (name == ".endm" || name == ".endmacro") {
        as.error(statement.location, name + " without .macro");
        return;
    }
    if (name == ".exitm") {
        if (expansion_depth == 0) as.error(statement.location, ".exitm outside a macro");
        else exit_macro = true;
        return;
    }
    if (name == ".purgem") {
        const auto macro_name = cursor.identifier();
        if (!macro_name || macros.erase(std::string(*macro_name)) == 0) {
            as.error(statement.location, "no such macro");
        }
        return;
    }
    if (name == ".rept") return repeat(statement);
    if (name == ".endr") {
        as.error(statement.location, ".endr without .rept");
        return;
    }
    if (name == ".error" || name == ".warning") {
        std::string message = name == ".error" ? ".error directive invoked"
                                               : ".warning directive invoked";
        if (auto text = cursor.string_literal(as.diagnostics_)) message = *text;
        if (name == ".error") as.error(statement.location, message);
        else as.warning(statement.location, message);
        return;
    }
    as.error(statement.location, "unknown directive '" + std::string(statement.name) + "'");
}

namespace {

std::string trim_text(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string{}
                                           : std::string(text.substr(first, last - first + 1));
}

} // namespace

bool Assembler::Impl::coff_directive(std::string_view name, Statement& statement) {
    auto& cursor = statement.operands;
    const auto expect_end = [&]() {
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after directive");
    };
    if (name == ".text" || name == ".data" || name == ".bss") {
        expect_end();
        const auto characteristics =
            name == ".text"   ? coff::scn_cnt_code | coff::scn_mem_execute | coff::scn_mem_read
            : name == ".data" ? coff::scn_cnt_initialized_data | coff::scn_mem_read | coff::scn_mem_write
                              : coff::scn_cnt_uninitialized_data | coff::scn_mem_read | coff::scn_mem_write;
        as.switch_section(as.coff_section(name, characteristics));
        return true;
    }
    if (name == ".section" || name == ".pushsection") {
        coff_section_directive(statement, name == ".pushsection");
        return true;
    }
    if (name == ".def") {
        const auto symbol_name = cursor.symbol_name();
        if (!symbol_name) {
            as.error(cursor.location(), "expected a symbol name");
            return true;
        }
        if (coff_definition) as.error(statement.location, "starting a new symbol definition without ending the previous one");
        coff_definition = as.symbol(*symbol_name, statement.location);
        expect_end();
        return true;
    }
    if (name == ".scl" || name == ".type") {
        const auto value = as.parse_absolute(cursor);
        if (!value) return true;
        if (!coff_definition) {
            as.error(statement.location, std::string(name) + " must appear inside a .def/.endef pair");
            return true;
        }
        auto& entry = as.symbol(*coff_definition);
        if (name == ".scl") {
            if (*value < 0 || *value > 255) {
                as.error(statement.location, "storage class value out of range");
                return true;
            }
            entry.storage_class = static_cast<std::uint8_t>(*value);
        } else {
            if (*value < 0 || *value > 0xffff) {
                as.error(statement.location, "type value out of range");
                return true;
            }
            entry.coff_type = static_cast<std::uint16_t>(*value);
        }
        expect_end();
        return true;
    }
    if (name == ".endef") {
        if (!coff_definition) as.error(statement.location, ".endef without .def");
        coff_definition.reset();
        expect_end();
        return true;
    }
    if (name == ".secrel32" || name == ".secidx" || name == ".rva") {
        const auto kind = name == ".secrel32" ? fixup::section_offset4
                          : name == ".secidx" ? fixup::section_index2
                                              : fixup::image_offset4;
        const unsigned size = name == ".secidx" ? 2 : 4;
        do {
            const auto location = cursor.location();
            const auto expression = as.parse_expression(cursor);
            if (!expression) return true;
            as.add_fixup(kind, *expression, location);
            const std::array<std::uint8_t, 4> zero{};
            as.emit_bytes(std::span(zero.data(), size));
        } while (cursor.consume(","));
        expect_end();
        return true;
    }
    if (name == ".local" || name == ".hidden" || name == ".internal" || name == ".protected" ||
        name == ".size" || name == ".ident") {
        as.error(statement.location, "directive '" + std::string(statement.name) +
                                         "' is not supported in COFF objects");
        return true;
    }
    return false;
}

void Assembler::Impl::coff_section_directive(Statement& statement, bool push) {
    auto& cursor = statement.operands;
    cursor.skip_space();
    std::string name;
    if (cursor.peek() == '"') {
        auto quoted = cursor.string_literal(as.diagnostics_);
        if (!quoted) return;
        name = *quoted;
    } else {
        const auto rest = cursor.rest();
        std::size_t end = 0;
        while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
        name = std::string(rest.substr(0, end));
        cursor.set_position(cursor.position() + end);
    }
    if (name.empty()) {
        as.error(statement.location, "expected a section name");
        return;
    }
    std::uint32_t characteristics =
        coff::scn_cnt_initialized_data | coff::scn_mem_read | coff::scn_mem_write;
    // The linker-directive section carries no contents and is removed; LLVM
    // MC gives it LNK_INFO | LNK_REMOVE.
    if (name == ".drectve") characteristics = coff::scn_lnk_info | coff::scn_lnk_remove;
    if (cursor.consume(",")) {
        cursor.skip_space();
        const auto flag_location = cursor.location();
        const auto text = cursor.string_literal(as.diagnostics_);
        if (!text) {
            as.error(flag_location, "expected a section flags string");
            return;
        }
        // GNU as and LLVM MC flag letters.
        enum : unsigned {
            alloc = 1, code = 2, load = 4, init_data = 8, shared = 16, no_load = 32,
            no_read = 64, no_write = 128, discardable = 256, info = 512,
        };
        unsigned flags = 0;
        bool read_only_removed = false;
        for (const char c : *text) {
            switch (c) {
            case 'a': break;
            case 'b':
                flags |= alloc;
                if ((flags & init_data) != 0) {
                    as.error(flag_location, "conflicting section flags 'b' and 'd'");
                    return;
                }
                flags &= ~load;
                break;
            case 'd':
                flags |= init_data;
                if ((flags & alloc) != 0) {
                    as.error(flag_location, "conflicting section flags 'b' and 'd'");
                    return;
                }
                flags &= ~no_write;
                if ((flags & no_load) == 0) flags |= load;
                break;
            case 'n': flags = (flags | no_load) & ~load; break;
            case 'D': flags |= discardable; break;
            case 'r':
                read_only_removed = false;
                flags |= no_write;
                if ((flags & code) == 0) flags |= init_data;
                if ((flags & no_load) == 0) flags |= load;
                break;
            case 's':
                flags = (flags | shared | init_data) & ~no_write;
                if ((flags & no_load) == 0) flags |= load;
                break;
            case 'w':
                flags &= ~no_write;
                read_only_removed = true;
                break;
            case 'x':
                flags |= code;
                if ((flags & no_load) == 0) flags |= load;
                if (!read_only_removed) flags |= no_write;
                break;
            case 'y': flags |= no_read | no_write; break;
            case 'i': flags |= info; break;
            default:
                as.error(flag_location, std::string("unknown section flag '") + c + "'");
                return;
            }
        }
        if (flags == 0) flags = init_data;
        characteristics = 0;
        if ((flags & code) != 0) characteristics |= coff::scn_cnt_code | coff::scn_mem_execute;
        if ((flags & init_data) != 0) characteristics |= coff::scn_cnt_initialized_data;
        if ((flags & alloc) != 0 && (flags & load) == 0) {
            characteristics |= coff::scn_cnt_uninitialized_data;
        }
        if ((flags & no_load) != 0) characteristics |= coff::scn_lnk_remove;
        if ((flags & discardable) != 0 || name.starts_with(".debug")) {
            characteristics |= coff::scn_mem_discardable;
        }
        if ((flags & no_read) == 0) characteristics |= coff::scn_mem_read;
        if ((flags & no_write) == 0) characteristics |= coff::scn_mem_write;
        if ((flags & shared) != 0) characteristics |= coff::scn_mem_shared;
        if ((flags & info) != 0) characteristics |= coff::scn_lnk_info;
    }
    std::optional<SymbolId> comdat;
    std::uint8_t selection = 0;
    if (cursor.consume(",")) {
        const auto type_location = cursor.location();
        const auto type = cursor.identifier();
        if (!type) {
            as.error(type_location, "expected a COMDAT type such as 'discard'");
            return;
        }
        if (*type == "one_only") selection = coff::comdat_select_noduplicates;
        else if (*type == "discard") selection = coff::comdat_select_any;
        else if (*type == "same_size") selection = coff::comdat_select_same_size;
        else if (*type == "same_contents") selection = coff::comdat_select_exact_match;
        else if (*type == "associative") selection = coff::comdat_select_associative;
        else if (*type == "largest") selection = coff::comdat_select_largest;
        else if (*type == "newest") selection = coff::comdat_select_newest;
        else {
            as.error(type_location, "unknown COMDAT type '" + std::string(*type) + "'");
            return;
        }
        if (!cursor.consume(",")) {
            as.error(cursor.location(), "expected ',' before the COMDAT symbol");
            return;
        }
        const auto symbol_name = cursor.symbol_name();
        if (!symbol_name) {
            as.error(cursor.location(), "expected the COMDAT symbol");
            return;
        }
        comdat = as.symbol(*symbol_name, statement.location);
    }
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after section directive");
        return;
    }
    const auto id = as.coff_section(name, characteristics, comdat, selection);
    if (push) as.push_section(id);
    else as.switch_section(id);
}

namespace {

struct MachOSectionDefault {
    std::string_view segment;
    std::string_view name;
    std::uint32_t flags;
};

// LLVM MC's predefined Darwin sections, whose flags apply when a directive
// names the section without a type.
constexpr MachOSectionDefault macho_defaults[] = {
    {"__TEXT", "__text", macho::s_attr_pure_instructions},
    {"__TEXT", "__eh_frame", macho::s_coalesced | macho::s_attr_no_toc |
                                 macho::s_attr_strip_static_syms | macho::s_attr_live_support},
    {"__TEXT", "__cstring", macho::s_cstring_literals},
    {"__TEXT", "__literal4", macho::s_4byte_literals},
    {"__TEXT", "__literal8", macho::s_8byte_literals},
    {"__TEXT", "__literal16", macho::s_16byte_literals},
    {"__TEXT", "__textcoal_nt", macho::s_coalesced | macho::s_attr_pure_instructions},
    {"__TEXT", "__const_coal", macho::s_coalesced},
    {"__DATA", "__datacoal_nt", macho::s_coalesced},
    {"__DATA", "__common", macho::s_zerofill},
    {"__DATA", "__bss", macho::s_zerofill},
    {"__DATA", "__thread_data", macho::s_thread_local_regular},
    {"__DATA", "__thread_bss", macho::s_thread_local_zerofill},
    {"__DATA", "__thread_vars", macho::s_thread_local_variables},
    {"__DATA", "__thread_init", macho::s_thread_local_init_function_pointers},
    {"__DATA", "__la_symbol_ptr", macho::s_lazy_symbol_pointers},
    {"__DATA", "__nl_symbol_ptr", macho::s_non_lazy_symbol_pointers},
    {"__DATA", "__thread_ptr", macho::s_thread_local_variable_pointers},
    {"__DATA", "__mod_init_func", macho::s_mod_init_func_pointers},
    {"__DATA", "__mod_term_func", macho::s_mod_term_func_pointers},
    {"__LD", "__compact_unwind", macho::s_attr_debug},
};

struct NamedFlag {
    std::string_view name;
    std::uint32_t value;
};

constexpr NamedFlag macho_types[] = {
    {"regular", macho::s_regular},
    {"zerofill", macho::s_zerofill},
    {"cstring_literals", macho::s_cstring_literals},
    {"4byte_literals", macho::s_4byte_literals},
    {"8byte_literals", macho::s_8byte_literals},
    {"literal_pointers", macho::s_literal_pointers},
    {"non_lazy_symbol_pointers", macho::s_non_lazy_symbol_pointers},
    {"lazy_symbol_pointers", macho::s_lazy_symbol_pointers},
    {"symbol_stubs", macho::s_symbol_stubs},
    {"mod_init_funcs", macho::s_mod_init_func_pointers},
    {"mod_term_funcs", macho::s_mod_term_func_pointers},
    {"coalesced", macho::s_coalesced},
    {"interposing", macho::s_interposing},
    {"16byte_literals", macho::s_16byte_literals},
    {"lazy_dylib_symbol_pointers", macho::s_lazy_dylib_symbol_pointers},
    {"thread_local_regular", macho::s_thread_local_regular},
    {"thread_local_zerofill", macho::s_thread_local_zerofill},
    {"thread_local_variables", macho::s_thread_local_variables},
    {"thread_local_variable_pointers", macho::s_thread_local_variable_pointers},
    {"thread_local_init_function_pointers", macho::s_thread_local_init_function_pointers},
};

constexpr NamedFlag macho_attributes[] = {
    {"pure_instructions", macho::s_attr_pure_instructions},
    {"no_toc", macho::s_attr_no_toc},
    {"strip_static_syms", macho::s_attr_strip_static_syms},
    {"no_dead_strip", macho::s_attr_no_dead_strip},
    {"live_support", macho::s_attr_live_support},
    {"self_modifying_code", macho::s_attr_self_modifying_code},
    {"debug", macho::s_attr_debug},
};

std::uint32_t macho_default_flags(std::string_view segment, std::string_view name) {
    if (segment == "__DWARF") return macho::s_attr_debug;
    for (const auto& entry : macho_defaults) {
        if (entry.segment == segment && entry.name == name) return entry.flags;
    }
    return macho::s_regular;
}

} // namespace

void Assembler::Impl::macho_section_directive(Statement& statement, bool push) {
    auto& cursor = statement.operands;
    // `.section segment,section[,type[,attribute+attribute...[,stub size]]]`
    std::vector<std::string> parts;
    {
        std::string current;
        for (const char c : cursor.rest()) {
            if (c == ',') {
                parts.push_back(trim_text(current));
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        parts.push_back(trim_text(current));
        cursor.set_position(cursor.text().size());
    }
    if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) {
        as.error(statement.location, "expected 'segment,section' in Mach-O section directive");
        return;
    }
    if (parts[0].size() > 16 || parts[1].size() > 16) {
        as.error(statement.location, "Mach-O segment and section names have at most 16 characters");
        return;
    }
    std::uint32_t flags = macho_default_flags(parts[0], parts[1]);
    if (parts.size() >= 3) {
        const auto found = std::find_if(std::begin(macho_types), std::end(macho_types),
                                        [&](const NamedFlag& entry) { return entry.name == parts[2]; });
        if (found == std::end(macho_types)) {
            as.error(statement.location, "unknown Mach-O section type '" + parts[2] + "'");
            return;
        }
        flags = found->value;
        if (parts.size() >= 4) {
            std::string_view attributes = parts[3];
            while (!attributes.empty()) {
                const auto plus = attributes.find('+');
                const auto attribute = trim_text(attributes.substr(0, plus));
                const auto match = std::find_if(std::begin(macho_attributes), std::end(macho_attributes),
                                                [&](const NamedFlag& entry) { return entry.name == attribute; });
                if (match == std::end(macho_attributes)) {
                    as.error(statement.location, "unknown Mach-O section attribute '" + attribute + "'");
                    return;
                }
                flags |= match->value;
                if (plus == std::string_view::npos) break;
                attributes.remove_prefix(plus + 1);
            }
        }
    }
    const auto id = as.macho_section(parts[0], parts[1], flags);
    if (push) as.push_section(id);
    else as.switch_section(id);
}

bool Assembler::Impl::macho_directive(std::string_view name, Statement& statement) {
    auto& cursor = statement.operands;
    const auto expect_end = [&]() {
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after directive");
    };
    struct Shorthand {
        std::string_view directive, segment, section;
    };
    static constexpr Shorthand shorthands[] = {
        {".text", "__TEXT", "__text"},         {".data", "__DATA", "__data"},
        {".bss", "__DATA", "__bss"},           {".const", "__TEXT", "__const"},
        {".const_data", "__DATA", "__const"},  {".cstring", "__TEXT", "__cstring"},
        {".literal4", "__TEXT", "__literal4"}, {".literal8", "__TEXT", "__literal8"},
        {".literal16", "__TEXT", "__literal16"},
        {".mod_init_func", "__DATA", "__mod_init_func"},
        {".mod_term_func", "__DATA", "__mod_term_func"},
        {".static_data", "__DATA", "__static_data"},
    };
    for (const auto& entry : shorthands) {
        if (entry.directive != name) continue;
        expect_end();
        as.switch_section(as.macho_section(entry.segment, entry.section,
                                           macho_default_flags(entry.segment, entry.section)));
        return true;
    }
    if (name == ".section" || name == ".pushsection") {
        macho_section_directive(statement, name == ".pushsection");
        return true;
    }
    if (name == ".private_extern" || name == ".weak_definition" || name == ".weak_reference" ||
        name == ".no_dead_strip" || name == ".alt_entry") {
        do {
            const auto location = cursor.location();
            const auto symbol_name = cursor.symbol_name();
            if (!symbol_name) {
                as.error(location, "expected a symbol name");
                return true;
            }
            auto& entry = as.symbol(as.symbol(*symbol_name, location));
            if (name == ".private_extern") {
                entry.private_extern = true;
                if (entry.binding == Symbol::Binding::Unset) entry.binding = Symbol::Binding::Global;
            } else if (name == ".weak_definition") {
                entry.weak_definition = true;
            } else if (name == ".weak_reference") {
                entry.weak_reference = true;
            } else if (name == ".no_dead_strip") {
                entry.no_dead_strip = true;
            } else {
                entry.alt_entry = true;
            }
        } while (cursor.consume(","));
        expect_end();
        return true;
    }
    if (name == ".subsections_via_symbols") {
        expect_end();
        subsections_via_symbols = true;
        return true;
    }
    if (name == ".zerofill") {
        // `.zerofill segment,section[,symbol,size[,align power]]`
        const auto segment = cursor.identifier();
        if (!segment || !cursor.consume(",")) {
            as.error(cursor.location(), "expected 'segment,section'");
            return true;
        }
        const auto section_name = cursor.identifier();
        if (!section_name) {
            as.error(cursor.location(), "expected a section name");
            return true;
        }
        if (segment->size() > 16 || section_name->size() > 16) {
            as.error(statement.location, "Mach-O segment and section names have at most 16 characters");
            return true;
        }
        const auto id = as.macho_section(*segment, *section_name, macho::s_zerofill);
        if (!cursor.consume(",")) {
            expect_end();
            return true;
        }
        const auto symbol_name = cursor.symbol_name();
        if (!symbol_name || !cursor.consume(",")) {
            as.error(cursor.location(), "expected 'symbol, size'");
            return true;
        }
        const auto size_value = as.parse_absolute(cursor);
        if (!size_value) return true;
        std::int64_t power = 0;
        if (cursor.consume(",")) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return true;
            power = *value;
        }
        expect_end();
        if (power < 0 || power > 15) {
            as.error(statement.location, "alignment power is out of range");
            return true;
        }
        const auto saved = as.current_;
        const auto saved_previous = previous;
        as.current_ = id;
        as.emit_alignment(std::uint64_t{1} << power, std::nullopt, 0);
        as.define_label(as.symbol(*symbol_name, statement.location), statement.location);
        auto& bytes = as.section(id).fragments.back().bytes;
        bytes.insert(bytes.end(), static_cast<std::size_t>(*size_value), 0);
        as.current_ = saved;
        previous = saved_previous;
        return true;
    }
    if (name == ".local" || name == ".hidden" || name == ".internal" || name == ".protected" ||
        name == ".weak" || name == ".size" || name == ".type" || name == ".ident") {
        as.error(statement.location, "directive '" + std::string(statement.name) +
                                         "' is not supported in Mach-O objects");
        return true;
    }
    if (name == ".build_version" || name == ".macosx_version_min" || name == ".ios_version_min" ||
        name == ".tvos_version_min" || name == ".watchos_version_min" || name == ".tbss" ||
        name == ".tlv" || name == ".indirect_symbol" || name == ".linker_option" ||
        name == ".lazy_reference" || name == ".reference" || name == ".desc") {
        as.error(statement.location, "unsupported Mach-O directive '" + std::string(statement.name) + "'");
        return true;
    }
    return false;
}

void Assembler::Impl::data(Statement& statement, unsigned size) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    if (cursor.at_end()) return;
    do {
        const auto location = cursor.location();
        const auto expression = as.parse_expression(cursor);
        if (!expression) return;
        if (size == 8 && !as.object_.elf64 && !as.absolute_now(*expression)) {
            // An ELF32 object has no eight-byte relocation.
            const auto value = as.evaluate(*expression, false);
            if (!value || !value->absolute()) {
                bool folds = false;
                if (value && value->add && value->subtract) {
                    const auto& left = as.symbols_[value->add->value];
                    const auto& right = as.symbols_[value->subtract->value];
                    folds = left.section && right.section && *left.section == *right.section;
                }
                if (!folds) {
                    as.error(location, "an eight-byte symbolic value needs a 64-bit object");
                    return;
                }
            }
        }
        as.emit_value(*expression, size, location);
    } while (cursor.consume(","));
    if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between values");
}

void Assembler::Impl::leb(Statement& statement, bool is_signed) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    do {
        const auto location = cursor.location();
        const auto expression = as.parse_expression(cursor);
        if (!expression) return;
        materialize_row();
        if (const auto value = as.absolute_now(*expression)) {
            std::vector<std::uint8_t> bytes;
            if (is_signed) append_sleb(bytes, *value);
            else append_uleb(bytes, static_cast<std::uint64_t>(*value));
            as.emit_bytes(bytes);
            continue;
        }
        Fragment fragment;
        fragment.kind = Fragment::Kind::Leb;
        fragment.expression = *expression;
        fragment.is_signed = is_signed;
        fragment.location = location;
        append_fragment(as.section(as.current_), std::move(fragment));
    } while (cursor.consume(","));
    if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between values");
}

void Assembler::Impl::ascii(Statement& statement, bool terminate) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    do {
        const auto text = cursor.string_literal(as.diagnostics_);
        if (!text) {
            as.error(cursor.location(), "expected a string");
            return;
        }
        std::vector<std::uint8_t> bytes(text->begin(), text->end());
        if (terminate) bytes.push_back(0);
        as.emit_bytes(bytes);
        cursor.skip_space();
    } while (cursor.consume(",") || cursor.peek() == '"');
    if (!cursor.at_end()) as.error(cursor.location(), "expected a string");
}

void Assembler::Impl::floating(Statement& statement, unsigned size) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    do {
        cursor.skip_space();
        const auto location = cursor.location();
        const auto rest = cursor.rest();
        std::size_t end = 0;
        while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
        auto token = std::string(rest.substr(0, end));
        cursor.set_position(cursor.position() + end);
        bool negative = false;
        std::string_view digits = token;
        if (!digits.empty() && (digits[0] == '-' || digits[0] == '+')) {
            negative = digits[0] == '-';
            digits.remove_prefix(1);
        }
        if (digits.size() > 2 && digits[0] == '0' && std::isalpha(static_cast<unsigned char>(digits[1]))) {
            digits.remove_prefix(2);  // GNU 0f/0d/0e prefixes.
        }
        double value = 0;
        const auto lowered = lower(digits);
        if (lowered == "inf" || lowered == "infinity") {
            value = std::numeric_limits<double>::infinity();
        } else if (lowered == "nan") {
            value = std::numeric_limits<double>::quiet_NaN();
        } else {
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
                as.error(location, "invalid floating-point constant '" + token + "'");
                return;
            }
        }
        if (negative) value = -value;
        if (size == 4) {
            as.emit_integer(std::bit_cast<std::uint32_t>(static_cast<float>(value)), 4);
        } else {
            as.emit_integer(std::bit_cast<std::uint64_t>(value), 8);
        }
    } while (cursor.consume(","));
    if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between values");
}

void Assembler::Impl::reserve(Statement& statement, bool zero) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    const auto location = cursor.location();
    const auto count = as.parse_expression(cursor);
    if (!count) return;
    std::uint8_t fill_byte = 0;
    if (cursor.consume(",")) {
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        fill_byte = static_cast<std::uint8_t>(*value);
    }
    if (const auto value = as.absolute_now(*count)) {
        if (*value < 0) {
            as.error(location, std::string(zero ? ".zero" : ".space") + " count is negative");
            return;
        }
        auto& current = as.section(as.current_);
        if (current.zero_fill && fill_byte != 0) {
            as.error(location, "a NOBITS section cannot hold nonzero data");
        }
        if (current.fragments.back().kind != Fragment::Kind::Data) append_fragment(current);
        auto& bytes = current.fragments.back().bytes;
        bytes.insert(bytes.end(), static_cast<std::size_t>(*value), fill_byte);
        return;
    }
    Fragment fragment;
    fragment.kind = Fragment::Kind::Fill;
    fragment.expression = *count;
    fragment.fill = fill_byte;
    fragment.location = location;
    append_fragment(as.section(as.current_), std::move(fragment));
}

void Assembler::Impl::fill(Statement& statement) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    const auto repeat_count = as.parse_absolute(cursor);
    if (!repeat_count) return;
    std::int64_t size = 1;
    std::int64_t value = 0;
    if (cursor.consume(",")) {
        const auto parsed = as.parse_absolute(cursor);
        if (!parsed) return;
        size = *parsed;
        if (cursor.consume(",")) {
            const auto parsed_value = as.parse_absolute(cursor);
            if (!parsed_value) return;
            value = *parsed_value;
        }
    }
    if (*repeat_count < 0 || size < 0) {
        as.error(statement.location, ".fill count and size must not be negative");
        return;
    }
    size = std::min<std::int64_t>(size, 8);
    for (std::int64_t index = 0; index < *repeat_count; ++index) {
        // GNU as stores the value in at most four bytes, zero-extended.
        std::uint64_t word = static_cast<std::uint32_t>(value);
        as.emit_integer(word, static_cast<unsigned>(size));
    }
}

void Assembler::Impl::align(Statement& statement, bool power) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    const auto location = cursor.location();
    const auto amount = as.parse_absolute(cursor);
    if (!amount) return;
    std::optional<std::uint8_t> fill_byte;
    std::uint64_t max_skip = 0;
    if (cursor.consume(",")) {
        if (cursor.peek() != ',') {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            fill_byte = static_cast<std::uint8_t>(*value);
        }
        if (cursor.consume(",")) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            max_skip = static_cast<std::uint64_t>(*value);
        }
    }
    std::uint64_t alignment = 1;
    if (power) {
        if (*amount < 0 || *amount > 32) {
            as.error(location, "alignment power is out of range");
            return;
        }
        alignment = std::uint64_t{1} << *amount;
    } else {
        if (*amount <= 0) {
            alignment = 1;
        } else if (!std::has_single_bit(static_cast<std::uint64_t>(*amount))) {
            as.error(location, "alignment is not a power of two");
            return;
        } else {
            alignment = static_cast<std::uint64_t>(*amount);
        }
    }
    as.emit_alignment(alignment, fill_byte, max_skip);
}

void Assembler::Impl::org(Statement& statement) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    const auto location = cursor.location();
    const auto target = as.parse_expression(cursor);
    if (!target) return;
    std::uint8_t fill_byte = 0;
    if (cursor.consume(",")) {
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        fill_byte = static_cast<std::uint8_t>(*value);
    }
    Fragment fragment;
    fragment.kind = Fragment::Kind::Org;
    fragment.expression = *target;
    fragment.fill = fill_byte;
    fragment.location = location;
    append_fragment(as.section(as.current_), std::move(fragment));
}

namespace {

std::optional<std::filesystem::path> find_file(
    const std::filesystem::path& name, const std::filesystem::path& directory,
    const std::vector<std::filesystem::path>& include_paths) {
    std::error_code error;
    if (name.is_absolute() || std::filesystem::exists(name, error)) return name;
    if (!directory.empty() && std::filesystem::exists(directory / name, error)) {
        return directory / name;
    }
    for (const auto& path : include_paths) {
        if (std::filesystem::exists(path / name, error)) return path / name;
    }
    return std::nullopt;
}

} // namespace

void Assembler::Impl::incbin(Statement& statement) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    const auto name = cursor.string_literal(as.diagnostics_);
    if (!name) {
        as.error(cursor.location(), "expected a file name");
        return;
    }
    std::int64_t skip = 0;
    std::optional<std::int64_t> count;
    if (cursor.consume(",")) {
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        skip = *value;
        if (cursor.consume(",")) {
            count = as.parse_absolute(cursor);
            if (!count) return;
        }
    }
    const auto path = find_file(*name, as.options_.source_directory, as.options_.include_paths);
    std::ifstream input;
    if (path) input.open(*path, std::ios::binary);
    if (!input) {
        as.error(statement.location, "cannot open '" + *name + "'");
        return;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    if (skip < 0 || static_cast<std::uint64_t>(skip) > bytes.size()) {
        as.error(statement.location, ".incbin skip is outside the file");
        return;
    }
    auto first = bytes.begin() + skip;
    auto last = bytes.end();
    if (count) {
        if (*count < 0 || static_cast<std::uint64_t>(*count) >
                              static_cast<std::uint64_t>(last - first)) {
            as.error(statement.location, ".incbin count is outside the file");
            return;
        }
        last = first + *count;
    }
    as.emit_bytes(std::vector<std::uint8_t>(first, last));
}

void Assembler::Impl::include(Statement& statement) {
    auto& cursor = statement.operands;
    const auto name = cursor.string_literal(as.diagnostics_);
    if (!name) {
        as.error(cursor.location(), "expected a file name");
        return;
    }
    std::filesystem::path directory = as.options_.source_directory;
    if (statement.location.file) directory = statement.location.file->path.parent_path();
    const auto path = find_file(*name, directory, as.options_.include_paths);
    std::string error;
    const SourceFile* file = path ? as.sources_.load(*path, error) : nullptr;
    if (!file) {
        as.error(statement.location, "cannot open '" + *name + "'");
        return;
    }
    if (++expansion_depth > 64) {
        as.error(statement.location, "includes nest too deeply");
    } else {
        const bool saved = in_block_comment;
        in_block_comment = false;
        process_text(file->text, file, 0, 1, std::nullopt);
        in_block_comment = saved;
    }
    --expansion_depth;
}

void Assembler::Impl::section_directive(Statement& statement, bool push) {
    auto& cursor = statement.operands;
    as.target_.flush(as);
    cursor.skip_space();
    std::string name;
    if (cursor.peek() == '"') {
        auto quoted = cursor.string_literal(as.diagnostics_);
        if (!quoted) return;
        name = *quoted;
    } else {
        const auto rest = cursor.rest();
        std::size_t end = 0;
        while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
        name = std::string(rest.substr(0, end));
        cursor.set_position(cursor.position() + end);
    }
    if (name.empty()) {
        as.error(statement.location, "expected a section name");
        return;
    }
    const auto has_prefix = [&](std::string_view prefix) {
        return name == prefix || (name.starts_with(prefix) && name[prefix.size()] == '.');
    };
    std::uint64_t flags = 0;
    std::uint32_t type = elf::sht_progbits;
    if (has_prefix(".rodata") || name == ".rodata1") {
        flags = elf::shf_alloc;
    } else if (name == ".init" || name == ".fini" || has_prefix(".text")) {
        flags = elf::shf_alloc | elf::shf_execinstr;
    } else if (has_prefix(".data") || name == ".data1" || has_prefix(".bss") ||
               has_prefix(".init_array") || has_prefix(".fini_array") ||
               has_prefix(".preinit_array")) {
        flags = elf::shf_alloc | elf::shf_write;
    } else if (has_prefix(".tdata") || has_prefix(".tbss")) {
        flags = elf::shf_alloc | elf::shf_write | elf::shf_tls;
    }
    bool type_given = false;
    bool flags_given = false;
    std::uint64_t entry_size = 0;
    std::optional<SymbolId> group;
    bool comdat = false;
    std::uint64_t unique = 0;
    std::int64_t subsection = 0;
    bool more = cursor.consume(",");
    // `.pushsection NAME, SUBSECTION[, FLAGS...]`
    if (more && push) {
        cursor.skip_space();
        if (digit(cursor.peek())) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            subsection = *value;
            more = cursor.consume(",");
        }
    }
    if (more) {
        cursor.skip_space();
        const auto flag_location = cursor.location();
        const auto text = cursor.string_literal(as.diagnostics_);
        if (!text) {
            as.error(flag_location, "expected a section flags string");
            return;
        }
        flags_given = true;
        for (const char c : *text) {
            switch (c) {
            case 'a': flags |= elf::shf_alloc; break;
            case 'w': flags |= elf::shf_write; break;
            case 'x': flags |= elf::shf_execinstr; break;
            case 'M': flags |= elf::shf_merge; break;
            case 'S': flags |= elf::shf_strings; break;
            case 'G': flags |= elf::shf_group; break;
            case 'T': flags |= elf::shf_tls; break;
            case 'R': flags |= elf::shf_gnu_retain; break;
            case 'e': flags |= elf::shf_exclude; break;
            default:
                as.error(flag_location, std::string("unknown section flag '") + c + "'");
                return;
            }
        }
        if (cursor.consume(",")) {
            cursor.skip_space();
            const auto type_location = cursor.location();
            if (!cursor.consume("@") && !cursor.consume("%")) {
                as.error(type_location, "expected '@' before the section type");
                return;
            }
            const auto rest = cursor.rest();
            std::size_t end = 0;
            while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
            const auto spelling = std::string(rest.substr(0, end));
            cursor.set_position(cursor.position() + end);
            type_given = true;
            if (spelling == "progbits") type = elf::sht_progbits;
            else if (spelling == "nobits") type = elf::sht_nobits;
            else if (spelling == "note") type = elf::sht_note;
            else if (spelling == "init_array") type = elf::sht_init_array;
            else if (spelling == "fini_array") type = elf::sht_fini_array;
            else if (spelling == "preinit_array") type = elf::sht_preinit_array;
            else {
                std::uint64_t number = 0;
                int base = 10;
                std::string_view digits = spelling;
                if (digits.starts_with("0x")) {
                    base = 16;
                    digits.remove_prefix(2);
                }
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number, base);
                if (spelling.empty() || parsed.ec != std::errc{} ||
                    parsed.ptr != digits.data() + digits.size()) {
                    as.error(type_location, "unknown section type '" + spelling + "'");
                    return;
                }
                type = static_cast<std::uint32_t>(number);
            }
            if ((flags & elf::shf_merge) != 0) {
                if (!cursor.consume(",")) {
                    as.error(cursor.location(), "expected the entry size of a mergeable section");
                    return;
                }
                const auto value = as.parse_absolute(cursor);
                if (!value) return;
                entry_size = static_cast<std::uint64_t>(*value);
            }
            if ((flags & elf::shf_group) != 0) {
                if (!cursor.consume(",")) {
                    as.error(cursor.location(), "expected a group signature");
                    return;
                }
                const auto signature = cursor.symbol_name();
                if (!signature) {
                    as.error(cursor.location(), "expected a group signature");
                    return;
                }
                group = as.symbol(*signature, statement.location);
                auto probe = cursor;
                if (probe.consume(",")) {
                    if (const auto word = probe.identifier(); word && *word == "comdat") {
                        comdat = true;
                        cursor = probe;
                    }
                }
            }
            if (cursor.consume(",")) {
                const auto word = cursor.identifier();
                if (!word || *word != "unique" || !cursor.consume(",")) {
                    as.error(cursor.location(), "expected 'unique, ID'");
                    return;
                }
                const auto value = as.parse_absolute(cursor);
                if (!value) return;
                unique = static_cast<std::uint64_t>(*value) + 1;
            }
        }
    }
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after section directive");
        return;
    }
    if (!type_given) {
        if (name.starts_with(".note")) type = elf::sht_note;
        else if (has_prefix(".init_array")) type = elf::sht_init_array;
        else if (has_prefix(".bss") || has_prefix(".tbss") || has_prefix(".sbss")) type = elf::sht_nobits;
        else if (has_prefix(".fini_array")) type = elf::sht_fini_array;
        else if (has_prefix(".preinit_array")) type = elf::sht_preinit_array;
    }
    flags &= ~elf::shf_group;
    const auto key = std::tuple{name, group ? group->value + 1 : 0U, unique};
    const auto existing = section_map.find(key);
    if (existing != section_map.end()) {
        const auto& entry = as.sections_[existing->second.value];
        if (flags_given &&
            (entry.flags & ~elf::shf_group) != as.target_.section_flags(name, flags)) {
            as.error(statement.location, "changed section flags for " + name);
            return;
        }
        if (type_given && entry.type != as.target_.section_type(name, type)) {
            as.error(statement.location, "changed section type for " + name);
            return;
        }
    }
    const auto id = as.section(name, type, flags, entry_size, group, comdat, unique);
    if (push) as.push_section(id, subsection);
    else as.switch_section(id);
}

void Assembler::Impl::symbol_binding(Statement& statement, Symbol::Binding binding) {
    auto& cursor = statement.operands;
    do {
        const auto location = cursor.location();
        const auto name = cursor.symbol_name();
        if (!name) {
            as.error(location, "expected a symbol name");
            return;
        }
        auto& entry = as.symbol(as.symbol(*name, location));
        entry.binding = binding;
    } while (cursor.consume(","));
    if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between symbols");
}

void Assembler::Impl::symbol_visibility(Statement& statement, std::uint8_t visibility) {
    auto& cursor = statement.operands;
    do {
        const auto location = cursor.location();
        const auto name = cursor.symbol_name();
        if (!name) {
            as.error(location, "expected a symbol name");
            return;
        }
        as.symbol(as.symbol(*name, location)).visibility = visibility;
    } while (cursor.consume(","));
    if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between symbols");
}

void Assembler::Impl::type(Statement& statement) {
    auto& cursor = statement.operands;
    const auto name = cursor.symbol_name();
    if (!name || !cursor.consume(",")) {
        as.error(cursor.location(), "expected 'symbol, type'");
        return;
    }
    cursor.skip_space();
    if (!cursor.consume("@") && !cursor.consume("%") && !cursor.consume("#")) {
        cursor.consume("\"");
    }
    const auto spelling = cursor.identifier();
    cursor.consume("\"");
    if (!spelling) {
        as.error(cursor.location(), "expected a symbol type");
        return;
    }
    std::uint8_t value = elf::stt_notype;
    const auto text = lower(*spelling);
    if (text == "function" || text == "stt_func") value = elf::stt_func;
    else if (text == "object" || text == "stt_object") value = elf::stt_object;
    else if (text == "tls_object" || text == "stt_tls") value = elf::stt_tls;
    else if (text == "common" || text == "stt_common") value = elf::stt_common;
    else if (text == "notype" || text == "stt_notype") value = elf::stt_notype;
    else if (text == "gnu_indirect_function") value = elf::stt_gnu_ifunc;
    else {
        as.error(statement.location, "unknown symbol type '" + std::string(*spelling) + "'");
        return;
    }
    as.symbol(as.symbol(*name, statement.location)).type = value;
}

void Assembler::Impl::size(Statement& statement) {
    auto& cursor = statement.operands;
    const auto name = cursor.symbol_name();
    if (!name || !cursor.consume(",")) {
        as.error(cursor.location(), "expected 'symbol, size'");
        return;
    }
    const auto value = as.parse_expression(cursor);
    if (!value) return;
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after expression");
        return;
    }
    as.symbol(as.symbol(*name, statement.location)).size = value;
}

void Assembler::Impl::common(Statement& statement, bool local) {
    auto& cursor = statement.operands;
    const auto name = cursor.symbol_name();
    if (!name || !cursor.consume(",")) {
        as.error(cursor.location(), "expected 'symbol, size'");
        return;
    }
    const auto size_value = as.parse_absolute(cursor);
    if (!size_value) return;
    std::int64_t alignment = 1;
    if (local && *size_value > 0) {
        alignment = static_cast<std::int64_t>(
            as.target_.local_common_alignment(static_cast<std::uint64_t>(*size_value)));
    }
    if (cursor.consume(",")) {
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        alignment = *value;
        // Mach-O alignments are powers of two.
        if (as.object_.format == Format::MachO) {
            if (alignment < 0 || alignment > 15) {
                as.error(statement.location, "alignment power is out of range");
                return;
            }
            alignment = std::int64_t{1} << alignment;
        }
    }
    if (alignment <= 0 || !std::has_single_bit(static_cast<std::uint64_t>(alignment))) {
        as.error(statement.location, "alignment must be a power of two");
        return;
    }
    const auto id = as.symbol(*name, statement.location);
    if (local) {
        const auto saved = as.current_;
        const auto saved_previous = previous;
        switch (as.object_.format) {
        case Format::Elf:
            as.current_ = as.section(".bss", elf::sht_nobits, elf::shf_alloc | elf::shf_write);
            break;
        case Format::Coff:
            as.current_ = as.coff_section(".bss", coff::scn_cnt_uninitialized_data |
                                                      coff::scn_mem_read | coff::scn_mem_write);
            break;
        case Format::MachO:
            as.current_ = as.macho_section("__DATA", "__bss", macho::s_zerofill);
            break;
        }
        as.emit_alignment(static_cast<std::uint64_t>(alignment), std::nullopt, 0);
        as.define_label(id, statement.location);
        auto& entry = as.symbol(id);
        if (entry.binding == Symbol::Binding::Unset) entry.binding = Symbol::Binding::Local;
        if (entry.type == elf::stt_notype) entry.type = elf::stt_object;
        if (!entry.size) entry.size = as.constant(*size_value, statement.location);
        auto& bss = as.section(as.current_).fragments.back().bytes;
        bss.insert(bss.end(), static_cast<std::size_t>(*size_value), 0);
        as.current_ = saved;
        previous = saved_previous;
        return;
    }
    auto& entry = as.symbol(id);
    if (entry.section) {
        as.error(statement.location, "symbol '" + *name + "' is already defined");
        return;
    }
    entry.common = true;
    entry.common_size = static_cast<std::uint64_t>(*size_value);
    entry.common_alignment = static_cast<std::uint64_t>(alignment);
    if (entry.type == elf::stt_notype) entry.type = elf::stt_object;
    if (entry.binding == Symbol::Binding::Unset) entry.binding = Symbol::Binding::Global;
}

void Assembler::Impl::ident(Statement& statement) {
    as.target_.flush(as);
    const auto text = statement.operands.string_literal(as.diagnostics_);
    if (!text) {
        as.error(statement.operands.location(), "expected a string");
        return;
    }
    const auto id = as.section(".comment", elf::sht_progbits, elf::shf_merge | elf::shf_strings, 1);
    auto& comment = as.section(id);
    auto& bytes = comment.fragments.back().bytes;
    if (bytes.empty()) bytes.push_back(0);
    bytes.insert(bytes.end(), text->begin(), text->end());
    bytes.push_back(0);
}

// ---------------------------------------------------------------------------
// DWARF line directives

void Assembler::Impl::file(Statement& statement) {
    auto& cursor = statement.operands;
    cursor.skip_space();
    if (cursor.peek() == '"') {
        const auto name = cursor.string_literal(as.diagnostics_);
        if (!name) return;
        if (!cursor.at_end()) {
            as.error(cursor.location(), "unexpected text after file name");
            return;
        }
        file_symbols.push_back(*name);
        file_symbol_order.push_back(static_cast<std::uint32_t>(as.symbols_.size()));
        return;
    }
    const auto number = as.parse_absolute(cursor);
    if (!number) return;
    if (*number < 0) {
        as.error(statement.location, "file number is negative");
        return;
    }
    auto first = cursor.string_literal(as.diagnostics_);
    if (!first) {
        as.error(cursor.location(), "expected a file name");
        return;
    }
    std::string directory;
    std::string name = *first;
    cursor.skip_space();
    if (cursor.peek() == '"') {
        directory = *first;
        auto second = cursor.string_literal(as.diagnostics_);
        if (!second) return;
        name = *second;
    }
    std::optional<std::array<std::uint8_t, 16>> md5;
    while (!cursor.at_end()) {
        const auto keyword = cursor.identifier();
        if (keyword && *keyword == "md5") {
            cursor.skip_space();
            auto rest = cursor.rest();
            if (!rest.starts_with("0x") && !rest.starts_with("0X")) {
                as.error(cursor.location(), "expected an MD5 checksum");
                return;
            }
            rest.remove_prefix(2);
            std::size_t end = 0;
            while (end < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[end]))) ++end;
            if (end != 32) {
                as.error(cursor.location(), "an MD5 checksum has 32 hexadecimal digits");
                return;
            }
            std::array<std::uint8_t, 16> sum{};
            for (unsigned index = 0; index < 16; ++index) {
                std::from_chars(rest.data() + index * 2, rest.data() + index * 2 + 2,
                                sum[index], 16);
            }
            md5 = sum;
            cursor.set_position(cursor.position() + 2 + end);
            continue;
        }
        as.error(cursor.location(), "unexpected text in .file directive");
        return;
    }
    line_table = true;
    const auto track_md5 = [&](bool used) {
        has_all_md5 = has_all_md5 && used;
    };
    if (*number == 0) {
        compilation_directory = directory;
        root_file = LineFile{name, 0, md5};
        track_md5(md5.has_value());
        return;
    }
    // LLVM MC's numbering: a directory equal to the root's is dropped, and a
    // name with a directory part is split into directory and base name.
    if (directory == compilation_directory) directory.clear();
    if (line_files.empty()) track_md5(md5.has_value());
    const auto index = static_cast<std::size_t>(*number);
    if (index >= line_files.size()) line_files.resize(index + 1);
    if (!line_files[index].name.empty()) {
        as.error(statement.location, "file number " + std::to_string(index) + " already allocated");
        return;
    }
    if (directory.empty()) {
        const std::filesystem::path path(name);
        const auto base = path.filename().generic_string();
        if (!base.empty()) {
            directory = path.parent_path().generic_string();
            if (!directory.empty()) name = base;
        }
    }
    unsigned directory_index = 0;
    if (!directory.empty()) {
        const auto found = std::find(line_directories.begin(), line_directories.end(), directory);
        directory_index = static_cast<unsigned>(found - line_directories.begin());
        if (found == line_directories.end()) line_directories.push_back(directory);
        ++directory_index;
    }
    line_files[index] = LineFile{name, directory_index, md5};
    track_md5(md5.has_value());
}

void Assembler::Impl::materialize_row() {
    if (!pending_row) return;
    auto row = *pending_row;
    pending_row.reset();
    row.label = as.temporary_label();
    const auto section = as.current_;
    auto found = std::find_if(line_sections.begin(), line_sections.end(),
                              [&](const auto& entry) { return entry.first == section; });
    if (found == line_sections.end()) {
        line_sections.emplace_back(section, std::vector<LineRow>{});
        found = line_sections.end() - 1;
    }
    found->second.push_back(row);
}

void Assembler::Impl::loc(Statement& statement) {
    auto& cursor = statement.operands;
    materialize_row();
    const auto file_number = as.parse_absolute(cursor);
    if (!file_number) return;
    LineRow row;
    row.file = static_cast<unsigned>(*file_number);
    if (*file_number != 0 && (static_cast<std::size_t>(*file_number) >= line_files.size() ||
                              line_files[static_cast<std::size_t>(*file_number)].name.empty())) {
        as.error(statement.location, "unassigned file number " + std::to_string(*file_number) +
                                         " in .loc directive");
        return;
    }
    if (*file_number == 0 && !root_file) {
        as.error(statement.location, "unassigned file number 0 in .loc directive");
        return;
    }
    const auto line = as.parse_absolute(cursor);
    if (!line) return;
    row.line = static_cast<unsigned>(*line);
    cursor.skip_space();
    if (!cursor.at_end() && (digit(cursor.peek()) || cursor.peek() == '(')) {
        const auto column = as.parse_absolute(cursor);
        if (!column) return;
        row.column = static_cast<unsigned>(*column);
    }
    // is_stmt persists from one .loc to the next; the other flags do not.
    row.flags = loc_is_stmt ? is_stmt : 0;
    while (!cursor.at_end()) {
        const auto word = cursor.identifier();
        if (!word) {
            as.error(cursor.location(), "unexpected text in .loc directive");
            return;
        }
        if (*word == "basic_block") row.flags |= basic_block;
        else if (*word == "prologue_end") row.flags |= prologue_end;
        else if (*word == "epilogue_begin") row.flags |= epilogue_begin;
        else if (*word == "is_stmt" || *word == "isa" || *word == "discriminator") {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            if (*word == "is_stmt") {
                if (*value == 0) row.flags &= static_cast<std::uint8_t>(~is_stmt);
                else if (*value == 1) row.flags |= is_stmt;
                else {
                    as.error(statement.location, "is_stmt value must be 0 or 1");
                    return;
                }
            } else if (*word == "isa") {
                row.isa = static_cast<unsigned>(*value);
            } else {
                row.discriminator = static_cast<unsigned>(*value);
            }
        } else if (*word == "view") {
            cursor.skip_space();
            const auto rest = cursor.rest();
            std::size_t end = 0;
            while (end < rest.size() && !space(rest[end])) ++end;
            cursor.set_position(cursor.position() + end);
        } else {
            as.error(statement.location, "unknown .loc option '" + std::string(*word) + "'");
            return;
        }
    }
    line_table = true;
    loc_is_stmt = (row.flags & is_stmt) != 0;
    pending_row = row;
}

// ---------------------------------------------------------------------------
// Call-frame directives

std::optional<unsigned> Assembler::Impl::cfi_register(Cursor& cursor) {
    cursor.skip_space();
    const auto location = cursor.location();
    if (digit(cursor.peek())) {
        const auto value = as.parse_absolute(cursor);
        if (!value) return std::nullopt;
        if (*value < 0) {
            as.error(location, "register number is negative");
            return std::nullopt;
        }
        return static_cast<unsigned>(*value);
    }
    const auto rest = cursor.rest();
    std::size_t end = 0;
    while (end < rest.size() && rest[end] != ',' && !space(rest[end])) ++end;
    const auto name = rest.substr(0, end);
    if (const auto number = as.target_.dwarf_register(name)) {
        cursor.set_position(cursor.position() + end);
        return number;
    }
    as.error(location, "unknown register '" + std::string(name) + "'");
    return std::nullopt;
}

void Assembler::Impl::cfi(Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    if (name == ".cfi_sections") {
        eh_frame = false;
        debug_frame = false;
        do {
            const auto section_name = cursor.identifier();
            if (section_name && *section_name == ".eh_frame") eh_frame = true;
            else if (section_name && *section_name == ".debug_frame") debug_frame = true;
            else {
                as.error(cursor.location(), "expected .eh_frame or .debug_frame");
                return;
            }
        } while (cursor.consume(","));
        return;
    }
    if (name == ".cfi_startproc") {
        if (open_frame) {
            as.error(statement.location, "starting a new frame before .cfi_endproc");
            return;
        }
        Frame frame;
        frame.section = as.current_;
        frame.begin = as.temporary_label();
        frame.return_address = as.target_.frame_facts().return_address;
        frame.location = statement.location;
        if (const auto word = cursor.identifier()) {
            if (*word != "simple") {
                as.error(statement.location, "expected 'simple'");
                return;
            }
            frame.simple = true;
        }
        open_frame = frames.size();
        frames.push_back(std::move(frame));
        return;
    }
    if (!open_frame) {
        as.error(statement.location, "this directive must appear between .cfi_startproc and .cfi_endproc");
        return;
    }
    auto& frame = frames[*open_frame];
    if (name == ".cfi_endproc") {
        frame.end = as.temporary_label();
        open_frame.reset();
        return;
    }
    if (name == ".cfi_signal_frame") {
        frame.signal = true;
        return;
    }
    if (name == ".cfi_return_column") {
        const auto reg = cfi_register(cursor);
        if (reg) frame.return_address = *reg;
        return;
    }
    using Op = FrameInstruction::Op;
    FrameInstruction instruction;
    const auto read_offset = [&]() -> bool {
        if (!cursor.consume(",")) {
            as.error(cursor.location(), "expected ','");
            return false;
        }
        const auto value = as.parse_absolute(cursor);
        if (!value) return false;
        instruction.offset = *value;
        return true;
    };
    if (name == ".cfi_def_cfa") {
        const auto reg = cfi_register(cursor);
        if (!reg || !read_offset()) return;
        instruction.op = Op::DefCfa;
        instruction.reg = *reg;
    } else if (name == ".cfi_def_cfa_register") {
        const auto reg = cfi_register(cursor);
        if (!reg) return;
        instruction.op = Op::DefCfaRegister;
        instruction.reg = *reg;
    } else if (name == ".cfi_def_cfa_offset" || name == ".cfi_adjust_cfa_offset") {
        const auto value = as.parse_absolute(cursor);
        if (!value) return;
        instruction.op = name == ".cfi_def_cfa_offset" ? Op::DefCfaOffset : Op::AdjustCfaOffset;
        instruction.offset = *value;
    } else if (name == ".cfi_offset" || name == ".cfi_rel_offset" || name == ".cfi_val_offset") {
        const auto reg = cfi_register(cursor);
        if (!reg || !read_offset()) return;
        instruction.op = name == ".cfi_offset"       ? Op::Offset
                         : name == ".cfi_rel_offset" ? Op::RelOffset
                                                     : Op::ValOffset;
        instruction.reg = *reg;
    } else if (name == ".cfi_restore" || name == ".cfi_undefined" || name == ".cfi_same_value") {
        do {
            const auto reg = cfi_register(cursor);
            if (!reg) return;
            FrameInstruction each;
            each.op = name == ".cfi_restore"     ? Op::Restore
                      : name == ".cfi_undefined" ? Op::Undefined
                                                 : Op::SameValue;
            each.reg = *reg;
            each.label = as.temporary_label();
            frame.instructions.push_back(std::move(each));
        } while (cursor.consume(","));
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after directive");
        return;
    } else if (name == ".cfi_register") {
        const auto first = cfi_register(cursor);
        if (!first || !cursor.consume(",")) return;
        const auto second = cfi_register(cursor);
        if (!second) return;
        instruction.op = Op::Register;
        instruction.reg = *first;
        instruction.reg2 = *second;
    } else if (name == ".cfi_remember_state") {
        instruction.op = Op::RememberState;
    } else if (name == ".cfi_restore_state") {
        instruction.op = Op::RestoreState;
    } else if (name == ".cfi_escape") {
        instruction.op = Op::Escape;
        do {
            const auto value = as.parse_absolute(cursor);
            if (!value) return;
            instruction.bytes.push_back(static_cast<std::uint8_t>(*value));
        } while (cursor.consume(","));
    } else {
        as.error(statement.location, "unsupported directive '" + std::string(statement.name) + "'");
        return;
    }
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after directive");
        return;
    }
    const auto alignment = as.target_.frame_facts().data_alignment;
    if ((instruction.op == Op::Offset || instruction.op == Op::ValOffset) &&
        instruction.offset % alignment != 0) {
        as.error(statement.location, "offset " + std::to_string(instruction.offset) +
                                         " is not a multiple of the data alignment factor " +
                                         std::to_string(alignment));
        return;
    }
    instruction.label = as.temporary_label();
    instruction.location = statement.location;
    frame.instructions.push_back(std::move(instruction));
}

// ---------------------------------------------------------------------------
// Layout

bool Assembler::Impl::layout(bool report) {
    layout_known = true;
    const auto pass = [&](bool last) -> std::pair<bool, bool> {
        bool changed = false;
        bool ok = true;
        for (std::size_t index = 0; index < as.sections_.size(); ++index) {
            auto& section = as.sections_[index];
            std::uint64_t address = 0;
            for (auto& fragment : section.fragments) {
                fragment.address = address;
                switch (fragment.kind) {
                case Fragment::Kind::Data:
                case Fragment::Kind::Relaxable:
                    fragment.size = fragment.bytes.size();
                    break;
                case Fragment::Kind::Align: {
                    const auto padding = (fragment.alignment - address % fragment.alignment) %
                                         fragment.alignment;
                    fragment.size = fragment.max_skip != 0 && padding > fragment.max_skip
                                        ? 0 : padding;
                    break;
                }
                case Fragment::Kind::Leb: {
                    const auto value = as.evaluate(fragment.expression, last);
                    std::int64_t number = 0;
                    if (value && value->absolute()) {
                        number = value->constant;
                    } else if (last) {
                        if (value) {
                            as.error(fragment.location,
                                     std::string(fragment.is_signed ? ".sleb128" : ".uleb128") +
                                         " expression is not absolute");
                        }
                        ok = false;
                    }
                    // A LEB never shrinks, so relaxation converges.
                    const auto previous_size = static_cast<unsigned>(fragment.bytes.size());
                    std::vector<std::uint8_t> bytes;
                    if (fragment.is_signed) append_sleb(bytes, number, previous_size);
                    else append_uleb(bytes, static_cast<std::uint64_t>(number), previous_size);
                    if (bytes != fragment.bytes) changed = true;
                    fragment.bytes = std::move(bytes);
                    fragment.size = fragment.bytes.size();
                    break;
                }
                case Fragment::Kind::Fill: {
                    const auto value = as.evaluate(fragment.expression, last);
                    std::uint64_t count = 0;
                    if (value && value->absolute() && value->constant >= 0) {
                        count = static_cast<std::uint64_t>(value->constant);
                    } else if (last) {
                        if (value) {
                            as.error(fragment.location,
                                     "fill count must be an absolute, nonnegative value");
                        }
                        ok = false;
                    }
                    if (count != fragment.size) changed = true;
                    fragment.size = count;
                    break;
                }
                case Fragment::Kind::Org: {
                    const auto value = as.evaluate(fragment.expression, last);
                    std::optional<std::uint64_t> target;
                    if (value && value->absolute()) {
                        target = static_cast<std::uint64_t>(value->constant);
                    } else if (value && value->add && !value->subtract) {
                        const auto& base = as.symbols_[value->add->value];
                        if (base.section && base.section->value == index) {
                            target = this->address(base) +
                                     static_cast<std::uint64_t>(value->constant);
                        }
                    }
                    std::uint64_t size_value = 0;
                    if (!target) {
                        if (last) {
                            as.error(fragment.location,
                                     ".org needs an offset in the current section");
                            ok = false;
                        }
                    } else if (*target < address) {
                        if (last) {
                            as.error(fragment.location,
                                     ".org cannot move the location backwards");
                            ok = false;
                        }
                    } else {
                        size_value = *target - address;
                    }
                    if (size_value != fragment.size) changed = true;
                    fragment.size = size_value;
                    break;
                }
                }
                address += fragment.size;
            }
            section.size = address;
        }
        return {changed, ok};
    };
    // Relaxes the instructions whose short form cannot reach their target
    // in the current layout; a relaxed instruction never shrinks back.
    const auto relax = [&]() {
        bool changed = false;
        for (std::size_t index = 0; index < as.sections_.size(); ++index) {
            for (auto& fragment : as.sections_[index].fragments) {
                if (fragment.kind != Fragment::Kind::Relaxable) continue;
                const auto& fixup = fragment.fixups.front();
                bool grow = true;
                const auto value = as.evaluate(fixup.expression, false, true);
                if (value && value->add && !value->subtract && value->specifier == 0 &&
                    resolves_in_place(*value, index, fixup)) {
                    const auto displacement =
                        static_cast<std::int64_t>(address(as.symbols_[value->add->value])) +
                        value->constant -
                        static_cast<std::int64_t>(fragment.address + fixup.offset) - 1;
                    grow = displacement < -128 || displacement > 127;
                }
                if (!grow) continue;
                fragment.kind = Fragment::Kind::Data;
                fragment.bytes = std::move(fragment.relaxed);
                fragment.fixups.assign(1, fragment.relaxed_fixup);
                changed = true;
            }
        }
        return changed;
    };
    // Relaxation only grows instructions, so it stops; other size changes
    // (alignment padding, sizes that depend on labels) may oscillate.
    for (unsigned unrelaxed = 0;;) {
        const bool sizes_changed = pass(false).first;
        const bool relaxed = relax();
        if (!relaxed && !sizes_changed) break;
        if (!relaxed && ++unrelaxed == 4096) {
            as.error({}, "section layout does not converge");
            return false;
        }
    }
    if (!report) return true;
    return pass(true).second;
}

void Assembler::Impl::merge_subsections() {
    for (std::size_t index = 0; index < as.sections_.size(); ++index) {
        auto& fragments = as.sections_[index].fragments;
        const auto first = fragments.front().subsection;
        if (std::all_of(fragments.begin(), fragments.end(),
                        [&](const Fragment& fragment) { return fragment.subsection == first; })) {
            continue;
        }
        std::vector<std::uint32_t> order(fragments.size());
        std::iota(order.begin(), order.end(), 0U);
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t left, std::uint32_t right) {
            return fragments[left].subsection < fragments[right].subsection;
        });
        std::vector<std::uint32_t> moved(fragments.size());
        std::vector<Fragment> sorted;
        sorted.reserve(fragments.size());
        for (std::uint32_t position = 0; position < order.size(); ++position) {
            moved[order[position]] = position;
            sorted.push_back(std::move(fragments[order[position]]));
        }
        for (auto& fragment : sorted) {
            for (auto& fixup : fragment.fixups) fixup.fragment = moved[fixup.fragment];
            fragment.relaxed_fixup.fragment = moved[fragment.relaxed_fixup.fragment];
        }
        fragments = std::move(sorted);
        // A section symbol keeps naming the section's start.
        for (auto& symbol : as.symbols_) {
            if (symbol.section && symbol.section->value == index &&
                symbol.type != elf::stt_section) {
                symbol.fragment = moved[symbol.fragment];
            }
        }
    }
    atom_definitions = ~0U;
}

std::optional<SymbolId> Assembler::Impl::atom(const Symbol& symbol) {
    if (!symbol.section || symbol.type == elf::stt_section) return std::nullopt;
    const SymbolId id{static_cast<std::uint32_t>(&symbol - as.symbols_.data())};
    if (!symbol.temporary && !symbol.alt_entry) return id;
    build_atoms();
    const auto& list = atoms[symbol.section->value];
    const auto key = std::tuple{symbol.fragment, symbol.offset, symbol.definition};
    const auto found = std::lower_bound(list.begin(), list.end(), key, [](const auto& entry, const auto& k) {
        return std::tuple{std::get<0>(entry), std::get<1>(entry), std::get<2>(entry)} < k;
    });
    if (found == list.begin()) return std::nullopt;
    return std::get<3>(*std::prev(found));
}

std::optional<SymbolId> Assembler::Impl::atom_at(std::size_t section_index, std::uint32_t fragment,
                                                 std::uint64_t offset) {
    build_atoms();
    const auto& list = atoms[section_index];
    const auto found = std::upper_bound(list.begin(), list.end(), std::pair{fragment, offset},
                                        [](const auto& k, const auto& entry) {
                                            return k < std::pair{std::get<0>(entry), std::get<1>(entry)};
                                        });
    if (found == list.begin()) return std::nullopt;
    return std::get<3>(*std::prev(found));
}

bool Assembler::Impl::resolves_in_place(const Value& value, std::size_t section_index,
                                        const Fixup& fixup) {
    if (!value.add || value.specifier != 0) return false;
    const auto* target = as.base_symbol(as.symbols_[value.add->value]);
    if (!target || !target->section || target->section->value != section_index) return false;
    const auto& symbol = as.symbols_[value.add->value];
    switch (as.object_.format) {
    case Format::Elf:
        // A global symbol may be preempted; an ifunc goes through the PLT.
        return (symbol.binding == Symbol::Binding::Local ||
                (symbol.binding == Symbol::Binding::Unset && symbol.defined())) &&
               symbol.type != elf::stt_gnu_ifunc;
    case Format::Coff:
        // Relocations to functions stay for the linker's incremental and
        // control-flow-guard tables, as in LLVM MC.
        return (symbol.coff_type >> 4U) != 2 && symbol.binding != Symbol::Binding::Weak;
    case Format::MachO:
        // Within one atom only.
        return atom(*target) == atom_at(section_index, fixup.fragment, fixup.offset);
    }
    return false;
}

namespace {

// DWARF line-program parameters shared by GNU as and LLVM MC.
constexpr int line_base = -5;
constexpr unsigned line_range = 14;
constexpr unsigned opcode_base = 13;

void encode_line_advance(std::vector<std::uint8_t>& out, std::int64_t line_delta,
                         std::uint64_t address_delta, bool end_sequence) {
    constexpr std::uint64_t max_special_address_delta = (255 - opcode_base) / line_range;
    if (end_sequence) {
        if (address_delta == max_special_address_delta) {
            out.push_back(8); // DW_LNS_const_add_pc
        } else if (address_delta != 0) {
            out.push_back(2); // DW_LNS_advance_pc
            append_uleb(out, address_delta);
        }
        out.push_back(0);
        out.push_back(1);
        out.push_back(1); // DW_LNE_end_sequence
        return;
    }
    auto temp = static_cast<std::uint64_t>(line_delta - line_base);
    bool need_copy = false;
    if (temp >= line_range || temp + opcode_base > 255) {
        out.push_back(3); // DW_LNS_advance_line
        append_sleb(out, line_delta);
        line_delta = 0;
        temp = static_cast<std::uint64_t>(0 - line_base);
        need_copy = true;
    }
    if (line_delta == 0 && address_delta == 0) {
        out.push_back(1); // DW_LNS_copy
        return;
    }
    temp += opcode_base;
    if (address_delta < 256 + max_special_address_delta) {
        auto opcode = temp + address_delta * line_range;
        if (opcode <= 255) {
            out.push_back(static_cast<std::uint8_t>(opcode));
            return;
        }
        opcode = temp + (address_delta - max_special_address_delta) * line_range;
        if (opcode <= 255) {
            out.push_back(8);
            out.push_back(static_cast<std::uint8_t>(opcode));
            return;
        }
    }
    out.push_back(2);
    append_uleb(out, address_delta);
    if (need_copy) out.push_back(1);
    else out.push_back(static_cast<std::uint8_t>(temp));
}

// Generated content and its fixups, appended to one section.
struct Builder {
    std::vector<std::uint8_t> bytes;
    struct Pending {
        std::size_t offset;
        FixupKind kind;
        ExpressionId expression;
    };
    std::vector<Pending> fixups;
    void u8(std::uint64_t value) { bytes.push_back(static_cast<std::uint8_t>(value)); }
};

} // namespace

SectionId Assembler::Impl::frame_section(bool eh) {
    switch (as.object_.format) {
    case Format::Elf:
        return eh ? as.section(".eh_frame", elf::sht_progbits, elf::shf_alloc)
                  : as.section(".debug_frame", elf::sht_progbits, 0);
    case Format::Coff:
        return eh ? as.coff_section(".eh_frame", coff::scn_cnt_initialized_data | coff::scn_mem_read)
                  : debug_section(".debug_frame");
    case Format::MachO:
        return eh ? as.macho_section("__TEXT", "__eh_frame", macho_default_flags("__TEXT", "__eh_frame"))
                  : debug_section(".debug_frame");
    }
    return {};
}

SectionId Assembler::Impl::debug_section(std::string_view name) {
    switch (as.object_.format) {
    case Format::Elf: return as.section(name, elf::sht_progbits, 0);
    case Format::Coff:
        return as.coff_section(name, coff::scn_cnt_initialized_data | coff::scn_mem_discardable |
                                         coff::scn_mem_read);
    case Format::MachO:
        // Mach-O section names have at most 16 characters: `.debug_x`
        // becomes `__debug_x`.
        return as.macho_section("__DWARF", ("__" + std::string(name.substr(1))).substr(0, 16),
                                macho::s_attr_debug);
    }
    return {};
}

void Assembler::Impl::emit_frames(bool eh) {
    const auto facts = as.target_.frame_facts();
    const unsigned address_size = as.object_.elf64 ? 8 : 4;
    const auto id = frame_section(eh);
    auto& section = as.sections_[id.value];
    const std::uint64_t base = section.size;
    Builder out;
    const auto align_to = [&](std::uint64_t alignment) {
        while ((base + out.bytes.size()) % alignment != 0) out.bytes.push_back(0);
        section.alignment = std::max(section.alignment, alignment);
    };
    const auto word = [&](std::uint64_t value, unsigned size) {
        std::array<std::uint8_t, 8> buffer{};
        as.write_integer(std::span(buffer.data(), size), value);
        out.bytes.insert(out.bytes.end(), buffer.begin(), buffer.begin() + size);
    };
    const auto patch = [&](std::size_t offset, std::uint64_t value) {
        as.write_integer(std::span(out.bytes.data() + offset, 4), value);
    };
    const auto reference = [&](SymbolId symbol, unsigned size, FixupKind kind,
                               std::int64_t addend) {
        auto expression = as.symbol_reference(symbol, {});
        if (addend != 0) expression = as.binary(Operator::Add, expression, as.constant(addend, {}));
        out.fixups.push_back({out.bytes.size(), kind, expression});
        word(0, size);
    };
    const std::uint8_t pointer_encoding = facts.eh_pointer_encoding;
    const unsigned pointer_size = eh ? ((pointer_encoding & 0x0f) == 0x0c ||
                                        (pointer_encoding & 0x0f) == 0x04 ? 8U
                                        : (pointer_encoding & 0x0f) == 0x00 ? address_size
                                                                            : 4U)
                                     : address_size;
    std::vector<const Frame*> ordered;
    for (const auto& frame : frames) ordered.push_back(&frame);
    const auto key = [&](const Frame* frame) {
        return eh ? std::tuple{frame->signal, frame->simple, frame->return_address}
                  : std::tuple{false, frame->simple, frame->return_address};
    };
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const Frame* left, const Frame* right) { return key(left) < key(right); });
    std::optional<std::tuple<bool, bool, unsigned>> last_key;
    std::size_t cie_offset = 0;
    std::int64_t initial_cfa_offset = 0;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto& frame = *ordered[index];
        if (!last_key || *last_key != key(&frame)) {
            last_key = key(&frame);
            cie_offset = out.bytes.size();
            word(0, 4);
            word(eh ? 0 : 0xffffffffU, 4);
            const unsigned version = eh ? 1 : 4;
            out.u8(version);
            if (eh) {
                out.u8('z');
                out.u8('R');
                if (frame.signal) out.u8('S');
            }
            out.u8(0);
            if (version >= 4) {
                out.u8(address_size);
                out.u8(0);
            }
            append_uleb(out.bytes, facts.code_alignment);
            append_sleb(out.bytes, facts.data_alignment);
            if (version == 1) out.u8(frame.return_address);
            else append_uleb(out.bytes, frame.return_address);
            if (eh) {
                append_uleb(out.bytes, 1);
                out.u8(pointer_encoding);
            }
            if (!frame.simple) {
                out.bytes.insert(out.bytes.end(), facts.initial_instructions.begin(),
                                 facts.initial_instructions.end());
            }
            initial_cfa_offset = frame.simple ? 0 : facts.initial_cfa_offset;
            align_to(eh ? 4 : address_size);
            patch(cie_offset, out.bytes.size() - cie_offset - 4);
        }
        const auto fde_offset = out.bytes.size();
        word(0, 4);
        if (eh) {
            word(out.bytes.size() - cie_offset, 4);
        } else {
            reference(section.symbol, 4, fixup::section_offset4,
                      static_cast<std::int64_t>(base + cie_offset));
        }
        const auto begin = address(as.symbols_[frame.begin.value]);
        const auto end = address(as.symbols_[frame.end->value]);
        if (eh && (pointer_encoding & 0x70) == 0x10) {
            reference(frame.begin, pointer_size,
                      pointer_size == 8 ? fixup::data8_pc : fixup::data4_pc, 0);
        } else {
            reference(frame.begin, pointer_size,
                      FixupKind{static_cast<std::uint16_t>(pointer_size)}, 0);
        }
        word(end - begin, pointer_size);
        if (eh) append_uleb(out.bytes, 0);
        std::int64_t cfa_offset = initial_cfa_offset;
        std::uint64_t location = begin;
        for (const auto& instruction : frame.instructions) {
            const auto here = address(as.symbols_[instruction.label.value]);
            if (here != location) {
                const auto delta = (here - location) / facts.code_alignment;
                if (delta < 64) {
                    out.u8(dw_cfa_advance_loc | delta);
                } else if (delta < 256) {
                    out.u8(0x02);
                    out.u8(delta);
                } else if (delta < 65536) {
                    out.u8(0x03);
                    word(delta, 2);
                } else {
                    out.u8(0x04);
                    word(delta, 4);
                }
                location = here;
            }
            using Op = FrameInstruction::Op;
            const auto factored = [&](std::int64_t offset) {
                return offset / facts.data_alignment;
            };
            switch (instruction.op) {
            case Op::DefCfa:
                out.u8(0x0c);
                append_uleb(out.bytes, instruction.reg);
                cfa_offset = instruction.offset;
                append_uleb(out.bytes, static_cast<std::uint64_t>(cfa_offset));
                break;
            case Op::DefCfaRegister:
                out.u8(0x0d);
                append_uleb(out.bytes, instruction.reg);
                break;
            case Op::DefCfaOffset:
            case Op::AdjustCfaOffset:
                out.u8(0x0e);
                cfa_offset = instruction.op == Op::DefCfaOffset ? instruction.offset
                                                                : cfa_offset + instruction.offset;
                append_uleb(out.bytes, static_cast<std::uint64_t>(cfa_offset));
                break;
            case Op::Offset:
            case Op::RelOffset: {
                auto offset = instruction.offset;
                if (instruction.op == Op::RelOffset) offset -= cfa_offset;
                offset = factored(offset);
                if (offset < 0) {
                    out.u8(0x11);
                    append_uleb(out.bytes, instruction.reg);
                    append_sleb(out.bytes, offset);
                } else if (instruction.reg < 64) {
                    out.u8(dw_cfa_offset | instruction.reg);
                    append_uleb(out.bytes, static_cast<std::uint64_t>(offset));
                } else {
                    out.u8(0x05);
                    append_uleb(out.bytes, instruction.reg);
                    append_uleb(out.bytes, static_cast<std::uint64_t>(offset));
                }
                break;
            }
            case Op::ValOffset: {
                const auto offset = factored(instruction.offset);
                out.u8(offset < 0 ? 0x15 : 0x14);
                append_uleb(out.bytes, instruction.reg);
                if (offset < 0) append_sleb(out.bytes, offset);
                else append_uleb(out.bytes, static_cast<std::uint64_t>(offset));
                break;
            }
            case Op::Restore:
                if (instruction.reg < 64) {
                    out.u8(dw_cfa_restore | instruction.reg);
                } else {
                    out.u8(0x06);
                    append_uleb(out.bytes, instruction.reg);
                }
                break;
            case Op::Undefined:
                out.u8(0x07);
                append_uleb(out.bytes, instruction.reg);
                break;
            case Op::SameValue:
                out.u8(0x08);
                append_uleb(out.bytes, instruction.reg);
                break;
            case Op::Register:
                out.u8(0x09);
                append_uleb(out.bytes, instruction.reg);
                append_uleb(out.bytes, instruction.reg2);
                break;
            case Op::RememberState: out.u8(0x0a); break;
            case Op::RestoreState: out.u8(0x0b); break;
            case Op::Escape:
                out.bytes.insert(out.bytes.end(), instruction.bytes.begin(), instruction.bytes.end());
                break;
            }
        }
        align_to(index + 1 == ordered.size() ? address_size : pointer_size);
        patch(fde_offset, out.bytes.size() - fde_offset - 4);
    }
    auto& fragments = section.fragments;
    if (fragments.back().kind != Fragment::Kind::Data) fragments.emplace_back();
    auto& fragment = fragments.back();
    const auto start = fragment.bytes.size();
    fragment.bytes.insert(fragment.bytes.end(), out.bytes.begin(), out.bytes.end());
    for (const auto& pending : out.fixups) {
        fragment.fixups.push_back({static_cast<std::uint32_t>(fragments.size() - 1),
                                   static_cast<std::uint32_t>(start + pending.offset),
                                   pending.kind, pending.expression, {}});
    }
    section.size += out.bytes.size();
}

void Assembler::Impl::emit_line_table() {
    if (!line_table) return;
    if (!as.target_.line_table_without_rows() &&
        std::all_of(line_sections.begin(), line_sections.end(),
                    [](const auto& entry) { return entry.second.empty(); })) {
        return;
    }
    const unsigned version = root_file ? 5 : 4;
    const auto facts = as.target_.frame_facts();
    const unsigned address_size = as.object_.elf64 ? 8 : 4;
    const auto id = debug_section(".debug_line");
    std::optional<SectionId> strings_id;
    std::vector<std::uint8_t> string_bytes;
    std::unordered_map<std::string, std::uint32_t> string_offsets;
    Builder out;
    const auto word = [&](std::uint64_t value, unsigned size) {
        std::array<std::uint8_t, 8> buffer{};
        as.write_integer(std::span(buffer.data(), size), value);
        out.bytes.insert(out.bytes.end(), buffer.begin(), buffer.begin() + size);
    };
    const auto line_string = [&](const std::string& text) {
        if (!strings_id) {
            strings_id = as.object_.format == Format::Elf
                             ? as.section(".debug_line_str", elf::sht_progbits,
                                          elf::shf_merge | elf::shf_strings, 1)
                             : debug_section(".debug_line_str");
        }
        const auto [entry, inserted] =
            string_offsets.try_emplace(text, static_cast<std::uint32_t>(string_bytes.size()));
        if (inserted) {
            string_bytes.insert(string_bytes.end(), text.begin(), text.end());
            string_bytes.push_back(0);
        }
        auto expression = as.symbol_reference(as.sections_[strings_id->value].symbol, {});
        if (entry->second != 0) {
            expression = as.binary(Operator::Add, expression, as.constant(entry->second, {}));
        }
        out.fixups.push_back({out.bytes.size(), fixup::section_offset4, expression});
        word(0, 4);
    };
    const auto c_string = [&](const std::string& text) {
        out.bytes.insert(out.bytes.end(), text.begin(), text.end());
        out.bytes.push_back(0);
    };
    word(0, 4);
    word(version, 2);
    if (version >= 5) {
        out.u8(address_size);
        out.u8(0);
    }
    const auto header_length = out.bytes.size();
    word(0, 4);
    out.u8(facts.code_alignment);
    out.u8(1);
    out.u8(1);
    out.u8(static_cast<std::uint8_t>(line_base));
    out.u8(line_range);
    out.u8(opcode_base);
    for (const unsigned length : {0U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 1U}) out.u8(length);
    if (version >= 5) {
        out.u8(1);
        append_uleb(out.bytes, 1);    // DW_LNCT_path
        append_uleb(out.bytes, 0x1f); // DW_FORM_line_strp
        append_uleb(out.bytes, line_directories.size() + 1);
        auto directory = compilation_directory;
        if (directory.empty()) {
            std::error_code error;
            directory = std::filesystem::current_path(error).generic_string();
        }
        line_string(directory);
        for (const auto& entry : line_directories) line_string(entry);
        out.u8(has_all_md5 ? 3 : 2);
        append_uleb(out.bytes, 1);
        append_uleb(out.bytes, 0x1f);
        append_uleb(out.bytes, 2);    // DW_LNCT_directory_index
        append_uleb(out.bytes, 0x0f); // DW_FORM_udata
        if (has_all_md5) {
            append_uleb(out.bytes, 5);    // DW_LNCT_MD5
            append_uleb(out.bytes, 0x1e); // DW_FORM_data16
        }
        append_uleb(out.bytes, line_files.empty() ? 1 : line_files.size());
        const auto entry = [&](const LineFile& file) {
            line_string(file.name);
            append_uleb(out.bytes, file.directory);
            if (has_all_md5) {
                const auto sum = file.md5.value_or(std::array<std::uint8_t, 16>{});
                out.bytes.insert(out.bytes.end(), sum.begin(), sum.end());
            }
        };
        entry(root_file ? *root_file : line_files[1]);
        for (std::size_t index = 1; index < line_files.size(); ++index) entry(line_files[index]);
    } else {
        for (const auto& entry : line_directories) c_string(entry);
        out.u8(0);
        for (std::size_t index = 1; index < line_files.size(); ++index) {
            c_string(line_files[index].name);
            append_uleb(out.bytes, line_files[index].directory);
            out.u8(0);
            out.u8(0);
        }
        out.u8(0);
    }
    as.write_integer(std::span(out.bytes.data() + header_length, 4),
                     out.bytes.size() - header_length - 4);
    for (const auto& [section_id, rows] : line_sections) {
        unsigned file_number = 1;
        unsigned last_line = 1;
        unsigned column = 0;
        std::uint8_t flags = is_stmt;
        unsigned isa = 0;
        unsigned discriminator = 0;
        std::optional<std::uint64_t> previous_address;
        for (const auto& row : rows) {
            const auto line_delta = static_cast<std::int64_t>(row.line) - last_line;
            if (file_number != row.file) {
                file_number = row.file;
                out.u8(4);
                append_uleb(out.bytes, file_number);
            }
            if (column != row.column) {
                column = row.column;
                out.u8(5);
                append_uleb(out.bytes, column);
            }
            if (discriminator != row.discriminator) {
                discriminator = row.discriminator;
                out.u8(0);
                append_uleb(out.bytes, uleb_size(discriminator) + 1);
                out.u8(4); // DW_LNE_set_discriminator
                append_uleb(out.bytes, discriminator);
            }
            if (isa != row.isa) {
                isa = row.isa;
                out.u8(12);
                append_uleb(out.bytes, isa);
            }
            if (((row.flags ^ flags) & is_stmt) != 0) {
                flags = row.flags;
                out.u8(6);
            }
            if ((row.flags & basic_block) != 0) out.u8(7);
            if ((row.flags & prologue_end) != 0) out.u8(10);
            if ((row.flags & epilogue_begin) != 0) out.u8(11);
            const auto here = address(as.symbols_[row.label.value]);
            if (!previous_address) {
                out.u8(0);
                append_uleb(out.bytes, address_size + 1);
                out.u8(2); // DW_LNE_set_address
                out.fixups.push_back({out.bytes.size(),
                                      FixupKind{static_cast<std::uint16_t>(address_size)},
                                      as.symbol_reference(row.label, {})});
                word(0, address_size);
                encode_line_advance(out.bytes, line_delta, 0, false);
            } else {
                encode_line_advance(out.bytes, line_delta,
                                    (here - *previous_address) / facts.code_alignment, false);
            }
            discriminator = 0;
            last_line = row.line;
            previous_address = here;
        }
        if (previous_address) {
            const auto end = as.sections_[section_id.value].size;
            encode_line_advance(out.bytes, 0, (end - *previous_address) / facts.code_alignment,
                                true);
        }
    }
    as.write_integer(std::span(out.bytes.data(), 4), out.bytes.size() - 4);
    auto& section = as.sections_[id.value];
    auto& fragments = section.fragments;
    if (fragments.back().kind != Fragment::Kind::Data) fragments.emplace_back();
    auto& fragment = fragments.back();
    const auto start = fragment.bytes.size();
    fragment.bytes.insert(fragment.bytes.end(), out.bytes.begin(), out.bytes.end());
    for (const auto& pending : out.fixups) {
        fragment.fixups.push_back({static_cast<std::uint32_t>(fragments.size() - 1),
                                   static_cast<std::uint32_t>(start + pending.offset),
                                   pending.kind, pending.expression, {}});
    }
    if (strings_id) {
        auto& strings_section = as.sections_[strings_id->value];
        auto& bytes = strings_section.fragments.back().bytes;
        bytes.insert(bytes.end(), string_bytes.begin(), string_bytes.end());
    }
}

// ---------------------------------------------------------------------------
// Fixups, symbols, and the object file

bool Assembler::Impl::apply(const Fixup& fixup, const FixupInfo& info, std::int64_t number,
                            std::span<std::uint8_t> bytes, bool relocated) {
    if (fixup.kind.value < FixupKind::target_base) {
        // COFF REL32 counts from the end of the field.
        if (relocated && as.object_.format == Format::Coff && fixup.kind == fixup::data4_pc) {
            number += 4;
        }
        if (!relocated && !fits(number, info.size)) {
            as.error(fixup.location, "value " + std::to_string(number) + " does not fit in " +
                                         std::to_string(info.size) + " bytes");
            return false;
        }
        as.write_integer(bytes, static_cast<std::uint64_t>(number));
        return true;
    }
    return as.target_.apply_fixup(as, fixup, number, bytes, relocated);
}

namespace {

bool local_binding(const Symbol& symbol) {
    return symbol.binding == Symbol::Binding::Local ||
           (symbol.binding == Symbol::Binding::Unset && symbol.defined());
}

} // namespace

bool Assembler::Impl::record_elf(std::size_t section_index, Fragment& fragment, Fixup& fixup,
                                 FixupInfo& info, Value& value, std::span<std::uint8_t> bytes,
                                 std::uint32_t sequence) {
    const auto place = fragment.address + fixup.offset;
    if (fixup.kind == fixup::section_offset4) fixup.kind = fixup::data4;
    if (value.subtract) {
        const auto& subtracted = as.symbols_[value.subtract->value];
        if (value.add && subtracted.section && subtracted.section->value == section_index &&
            (fixup.kind == fixup::data4 || fixup.kind == fixup::data8)) {
            value.constant += static_cast<std::int64_t>(place) -
                              static_cast<std::int64_t>(address(subtracted));
            value.subtract.reset();
            fixup.kind = fixup.kind == fixup::data4 ? fixup::data4_pc : fixup::data8_pc;
            info.pc_relative = true;
        } else {
            as.error(fixup.location, "cannot represent a difference of symbols in different sections");
            return false;
        }
    }
    if (value.absolute()) {
        if (info.pc_relative) {
            as.error(fixup.location, "the target of a PC-relative field must be a symbol");
            return false;
        }
        return apply(fixup, info, value.constant, bytes, false);
    }
    const auto& target_symbol = as.symbols_[value.add->value];
    if (info.pc_relative && !info.relocate &&
        resolves_in_place(value, section_index, fixup)) {
        const auto* base = as.base_symbol(target_symbol);
        return apply(fixup, info,
                     static_cast<std::int64_t>(address(*base)) + value.constant -
                         static_cast<std::int64_t>(place),
                     bytes, false);
    }
    if (!target_symbol.defined() && target_symbol.temporary) {
        as.error(fixup.location, "undefined local symbol '" + target_symbol.name + "'");
        return false;
    }
    const Section* symbol_section =
        target_symbol.section ? &as.sections_[target_symbol.section->value] : nullptr;
    const auto choice = as.target_.relocation(as, fixup, value, target_symbol, symbol_section);
    if (!choice) return false;
    SymbolId relocated = *value.add;
    std::int64_t addend = value.constant;
    if (target_symbol.section && local_binding(target_symbol) &&
        target_symbol.type != elf::stt_section) {
        const bool keep = choice->keep_symbol ||
                          ((symbol_section->flags & elf::shf_merge) != 0 && addend != 0);
        if (!keep) {
            relocated = symbol_section->symbol;
            addend += static_cast<std::int64_t>(address(target_symbol));
        }
    }
    as.symbols_[relocated.value].used_in_relocation = true;
    if (as.object_.rela) {
        addend += choice->rela_bias;
    } else if (!apply(fixup, info, addend, bytes, true)) {
        return false;
    }
    Relocation record{place, choice->type, relocated, addend};
    record.sequence = sequence;
    record.target = *value.add;
    record.target_addend = value.constant;
    relocations[section_index].push_back(record);
    return true;
}

bool Assembler::Impl::record_coff(std::size_t section_index, Fragment& fragment, Fixup& fixup,
                                  FixupInfo& info, Value& value, std::span<std::uint8_t> bytes) {
    const auto place = fragment.address + fixup.offset;
    std::int64_t field = value.constant;
    if (value.subtract) {
        // A - B with B in this section: a PC-relative field.
        const auto& subtracted = as.symbols_[value.subtract->value];
        if (!value.add || !subtracted.section || subtracted.section->value != section_index ||
            !(fixup.kind == fixup::data4 || fixup.kind == fixup::data8)) {
            as.error(fixup.location, "cannot represent this difference of symbols in COFF");
            return false;
        }
        field += static_cast<std::int64_t>(place) - static_cast<std::int64_t>(address(subtracted));
        value.subtract.reset();
        fixup.kind = fixup::data4_pc;
        info.pc_relative = true;
    }
    if (value.absolute()) {
        if (info.pc_relative) {
            as.error(fixup.location, "the target of a PC-relative field must be a symbol");
            return false;
        }
        if (fixup.kind == fixup::section_offset4 || fixup.kind == fixup::section_index2 ||
            fixup.kind == fixup::image_offset4) {
            as.error(fixup.location, "this directive needs a symbol");
            return false;
        }
        return apply(fixup, info, field, bytes, false);
    }
    const auto& target_symbol = as.symbols_[value.add->value];
    if (info.pc_relative && resolves_in_place(value, section_index, fixup)) {
        const auto* base = as.base_symbol(target_symbol);
        return apply(fixup, info,
                     static_cast<std::int64_t>(address(*base)) + field -
                         static_cast<std::int64_t>(place),
                     bytes, false);
    }
    if (!target_symbol.defined() && target_symbol.temporary) {
        as.error(fixup.location, "undefined local symbol '" + target_symbol.name + "'");
        return false;
    }
    const auto* base = as.base_symbol(target_symbol);
    const Section* symbol_section =
        base && base->section ? &as.sections_[base->section->value] : nullptr;
    const auto choice = as.target_.relocation(as, fixup, value, target_symbol, symbol_section);
    if (!choice) return false;
    SymbolId relocated = *value.add;
    if (target_symbol.temporary && base && base->section) {
        // Temporary labels are not in the symbol table: name the section.
        relocated = symbol_section->symbol;
        field += static_cast<std::int64_t>(address(*base));
    }
    if (fixup.kind == fixup::section_index2) field = 0;
    as.symbols_[relocated.value].used_in_relocation = true;
    if (!apply(fixup, info, field, bytes, true)) return false;
    relocations[section_index].push_back({place, choice->type, relocated, 0});
    return true;
}

bool Assembler::Impl::resolve_fixups() {
    bool ok = true;
    for (std::size_t section_index = 0; section_index < as.sections_.size(); ++section_index) {
        auto& section = as.sections_[section_index];
        std::uint32_t sequence = 0;
        for (auto& fragment : section.fragments) {
            for (const auto& original : fragment.fixups) {
                auto fixup = original;
                ++sequence;
                FixupInfo info;
                if (fixup.kind.value < FixupKind::target_base) {
                    info.pc_relative = fixup.kind == fixup::data4_pc || fixup.kind == fixup::data8_pc;
                    info.size = fixup.kind == fixup::data8_pc            ? 8U
                                : fixup.kind == fixup::section_index2    ? 2U
                                : fixup.kind.value > 8 ? 4U : fixup.kind.value;
                } else {
                    info = as.target_.fixup_info(fixup.kind);
                }
                std::span<std::uint8_t> bytes(fragment.bytes.data() + fixup.offset, info.size);
                if (section.zero_fill) {
                    as.error(fixup.location, "a zero-fill section cannot hold relocated data");
                    ok = false;
                    continue;
                }
                auto value = as.evaluate(fixup.expression, true, true);
                if (!value) {
                    ok = false;
                    continue;
                }
                switch (as.object_.format) {
                case Format::Elf:
                    if (!record_elf(section_index, fragment, fixup, info, *value, bytes, sequence)) {
                        ok = false;
                    }
                    break;
                case Format::Coff:
                    if (!record_coff(section_index, fragment, fixup, info, *value, bytes)) ok = false;
                    break;
                case Format::MachO: {
                    const auto place = fragment.address + fixup.offset;
                    if (value->absolute() && !info.pc_relative &&
                        fixup.kind != fixup::section_offset4) {
                        if (!apply(fixup, info, value->constant, bytes, false)) ok = false;
                        break;
                    }
                    if (fixup.kind == fixup::section_offset4 && value->add && !value->subtract) {
                        // DWARF section offsets are plain numbers in Mach-O.
                        const auto* base = as.base_symbol(as.symbols_[value->add->value]);
                        if (!base || !base->section) {
                            as.error(fixup.location, "a section offset needs a defined symbol");
                            ok = false;
                            break;
                        }
                        if (!apply(fixup, info,
                                   static_cast<std::int64_t>(address(*base)) + value->constant,
                                   bytes, false)) {
                            ok = false;
                        }
                        break;
                    }
                    if (info.pc_relative && !value->subtract &&
                        resolves_in_place(*value, section_index, fixup)) {
                        const auto* base = as.base_symbol(as.symbols_[value->add->value]);
                        if (!apply(fixup, info,
                                   static_cast<std::int64_t>(address(*base)) + value->constant -
                                       static_cast<std::int64_t>(place),
                                   bytes, false)) {
                            ok = false;
                        }
                        break;
                    }
                    std::int64_t field = 0;
                    std::vector<Relocation> records;
                    const auto field_address =
                        as.sections_[section_index].size == 0 ? place : place;  // section-relative
                    if (!as.target_.macho_relocation(as, SectionId{static_cast<std::uint32_t>(section_index)},
                                                     fixup, place, field_address, *value, field, records)) {
                        ok = false;
                        break;
                    }
                    // The target computed the field's exact in-place value.
                    as.write_integer(bytes, static_cast<std::uint64_t>(field));
                    for (auto& record : records) {
                        as.symbols_[record.symbol.value].used_in_relocation = true;
                        relocations[section_index].push_back(record);
                    }
                    break;
                }
                }
            }
        }
    }
    for (auto& entries : relocations) {
        if (as.object_.format == Format::Elf && !as.object_.rela) {
            as.target_.order_relocations(entries);
        } else {
            std::stable_sort(entries.begin(), entries.end(),
                             [](const Relocation& left, const Relocation& right) {
                                 return left.offset < right.offset;
                             });
        }
    }
    return ok;
}

std::vector<std::uint8_t> Assembler::section_contents(SectionId id) const {
    const auto& section = sections_[id.value];
    std::vector<std::uint8_t> data;
    data.reserve(section.size);
    for (const auto& fragment : section.fragments) {
        switch (fragment.kind) {
        case Fragment::Kind::Data:
        case Fragment::Kind::Leb:
        case Fragment::Kind::Relaxable:
            data.insert(data.end(), fragment.bytes.begin(), fragment.bytes.end());
            break;
        case Fragment::Kind::Align:
        case Fragment::Kind::Fill:
        case Fragment::Kind::Org: {
            const auto start = data.size();
            data.insert(data.end(), fragment.size, fragment.code_fill ? 0 : fragment.fill);
            if (fragment.code_fill) {
                target_.code_padding(std::span(data.data() + start, fragment.size));
            }
            break;
        }
        }
    }
    return data;
}

std::vector<std::uint8_t> Assembler::Impl::write() {
    switch (as.object_.format) {
    case Format::Elf: return write_elf_object();
    case Format::Coff: return write_coff(as, relocations, file_symbols);
    case Format::MachO: return write_macho(as, relocations, subsections_via_symbols);
    }
    return {};
}

std::vector<std::uint8_t> Assembler::Impl::write_elf_object() {
    const bool wide = as.object_.elf64;
    const bool big = as.object_.big_endian;
    StringTable strings;

    // Section header indices: null, .strtab, then each section preceded by
    // its group and followed by its relocations, then .symtab.
    std::vector<std::uint32_t> section_index(as.sections_.size());
    std::vector<std::uint32_t> relocation_index(as.sections_.size());
    std::map<std::uint32_t, std::uint32_t> group_index;
    std::uint32_t next = 2;
    for (std::size_t index = 0; index < as.sections_.size(); ++index) {
        const auto& section = as.sections_[index];
        if (section.group && !group_index.contains(section.group->value)) {
            group_index[section.group->value] = next++;
        }
        section_index[index] = next++;
        if (!relocations[index].empty()) relocation_index[index] = next++;
    }
    const auto symtab_index = next;
    if (symtab_index >= 0xff00) {
        as.error({}, "the object would have " + std::to_string(symtab_index + 1) +
                         " sections; ELF section indices beyond 65279 are not supported");
        return {};
    }

    // Symbols: locals in creation order, preceded by file symbols, then
    // globals in creation order.
    const auto local = [](const Symbol& symbol) {
        return symbol.binding == Symbol::Binding::Local ||
               (symbol.binding == Symbol::Binding::Unset && symbol.defined());
    };
    std::vector<std::uint32_t> symbol_index(as.symbols_.size());
    ByteWriter symtab(big);
    const auto symbol_entry = [&](std::uint32_t name, std::uint64_t value, std::uint64_t size,
                                  std::uint8_t info, std::uint8_t other, std::uint16_t shndx) {
        if (wide) {
            symtab.u32(name);
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
            symtab.u64(value);
            symtab.u64(size);
        } else {
            symtab.u32(name);
            symtab.u32(value);
            symtab.u32(size);
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
        }
    };
    std::uint32_t count = 0;
    symbol_entry(0, 0, 0, 0, 0, 0);
    ++count;
    constexpr std::uint16_t shn_abs = 0xfff1;
    constexpr std::uint16_t shn_common = 0xfff2;
    struct Resolved {
        std::uint64_t value{};
        std::uint16_t shndx{};
        const Symbol* base{};
    };
    const auto resolve = [&](const Symbol& symbol) -> std::optional<Resolved> {
        if (symbol.common) return Resolved{symbol.common_alignment, shn_common, &symbol};
        if (symbol.section) {
            return Resolved{address(symbol),
                            static_cast<std::uint16_t>(section_index[symbol.section->value]),
                            &symbol};
        }
        if (symbol.variable) {
            const auto value = as.evaluate(*symbol.variable, true);
            if (!value) return std::nullopt;
            if (value->absolute()) {
                return Resolved{static_cast<std::uint64_t>(value->constant), shn_abs, nullptr};
            }
            if (value->add && !value->subtract) {
                const auto& base = as.symbols_[value->add->value];
                if (base.section) {
                    return Resolved{address(base) + static_cast<std::uint64_t>(value->constant),
                                    static_cast<std::uint16_t>(section_index[base.section->value]),
                                    &base};
                }
            }
            as.error(symbol.location, "symbol '" + symbol.name +
                                          "' is not an absolute value or an offset from a label");
            return std::nullopt;
        }
        return Resolved{0, 0, nullptr};
    };
    const auto emit_symbol = [&](std::size_t index, std::uint8_t binding) {
        const auto& symbol = as.symbols_[index];
        const auto resolved = resolve(symbol);
        if (!resolved) return;
        std::uint8_t type = symbol.type;
        std::uint64_t size = symbol.common ? symbol.common_size : 0;
        const ExpressionId* size_expression = symbol.size ? &*symbol.size : nullptr;
        if (resolved->base && resolved->base != &symbol) {
            if (type == elf::stt_notype) type = resolved->base->type;
            if (!size_expression && resolved->base->size) size_expression = &*resolved->base->size;
        }
        // A label in a TLS section is a TLS symbol, as GNU as and LLVM MC
        // make it.
        if ((type == elf::stt_notype || type == elf::stt_object) && symbol.section &&
            (as.sections_[symbol.section->value].flags & elf::shf_tls) != 0) {
            type = elf::stt_tls;
        }
        if (size_expression) {
            const auto value = as.evaluate(*size_expression, true);
            if (value && value->absolute()) {
                size = static_cast<std::uint64_t>(value->constant);
            } else if (value) {
                as.error(symbol.location, "size of symbol '" + symbol.name + "' is not absolute");
            }
        }
        const auto name = symbol.type == elf::stt_section ? 0U : strings.add(symbol.name);
        symbol_index[index] = count++;
        symbol_entry(name, resolved->value, size, static_cast<std::uint8_t>((binding << 4U) | type),
                     static_cast<std::uint8_t>(symbol.visibility | symbol.other), resolved->shndx);
    };
    // A group signature that nothing defines or references becomes a local
    // symbol in its group section.
    std::map<std::uint32_t, std::uint16_t> lone_signatures;
    for (const auto& section : as.sections_) {
        if (!section.group) continue;
        const auto& signature = as.symbols_[section.group->value];
        if (!signature.defined() && !signature.used_in_relocation &&
            signature.binding == Symbol::Binding::Unset) {
            lone_signatures[section.group->value] =
                static_cast<std::uint16_t>(group_index[section.group->value]);
        }
    }
    std::size_t file_cursor = 0;
    for (std::size_t index = 0; index < as.symbols_.size(); ++index) {
        const auto& symbol = as.symbols_[index];
        bool include = false;
        if (const auto lone = lone_signatures.find(static_cast<std::uint32_t>(index));
            lone != lone_signatures.end()) {
            symbol_index[index] = count++;
            symbol_entry(strings.add(symbol.name), 0, 0, elf::stt_notype, 0, lone->second);
            continue;
        }
        if (symbol.type == elf::stt_section) {
            include = symbol.used_in_relocation;
        } else if (symbol.temporary) {
            include = symbol.used_in_relocation && symbol.defined() && local(symbol);
        } else {
            include = local(symbol);
            if (symbol.binding == Symbol::Binding::Local && !symbol.defined()) {
                as.error(symbol.location, "local symbol '" + symbol.name + "' is not defined");
                include = false;
            }
        }
        if (!include) continue;
        for (; file_cursor < file_symbols.size() && file_symbol_order[file_cursor] <= index;
             ++file_cursor) {
            symbol_entry(strings.add(file_symbols[file_cursor]), 0, 0,
                         elf::stt_file, 0, shn_abs);
            ++count;
        }
        emit_symbol(index, elf::stb_local);
    }
    for (; file_cursor < file_symbols.size(); ++file_cursor) {
        symbol_entry(strings.add(file_symbols[file_cursor]), 0, 0, elf::stt_file, 0, shn_abs);
        ++count;
    }
    const auto first_global = count;
    for (std::size_t index = 0; index < as.symbols_.size(); ++index) {
        const auto& symbol = as.symbols_[index];
        if (symbol.type == elf::stt_section || local(symbol)) continue;
        if (symbol.binding == Symbol::Binding::Unset && !symbol.used_in_relocation) continue;
        if (symbol.temporary) continue;
        emit_symbol(index, symbol.binding == Symbol::Binding::Weak ? elf::stb_weak : elf::stb_global);
    }

    std::vector<ElfSection> output(symtab_index);
    auto& strtab = output[0];
    strtab.type = elf::sht_strtab;
    for (std::size_t index = 0; index < as.sections_.size(); ++index) {
        const auto& section = as.sections_[index];
        if (section.group && group_index[section.group->value] != 0) {
            auto& group = output[group_index[section.group->value] - 1];
            if (group.type != elf::sht_group) {
                group.type = elf::sht_group;
                group.name = strings.add(".group");
                group.alignment = 4;
                group.entry_size = 4;
                group.link = symtab_index;
                group.info = symbol_index[section.group->value];
                ByteWriter words(big);
                words.u32(section.comdat ? 1 : 0);
                group.data = std::move(words.data());
            }
            ByteWriter words(big);
            words.u32(section_index[index]);
            if (relocation_index[index] != 0) words.u32(relocation_index[index]);
            group.data.insert(group.data.end(), words.data().begin(), words.data().end());
        }
        auto& entry = output[section_index[index] - 1];
        entry.name = strings.add(section.name);
        entry.type = section.type;
        entry.flags = section.flags;
        entry.alignment = section.alignment;
        entry.entry_size = section.entry_size;
        if (section.type == elf::sht_nobits) {
            entry.size = section.size;
            for (const auto& fragment : section.fragments) {
                if (std::any_of(fragment.bytes.begin(), fragment.bytes.end(),
                                [](std::uint8_t byte) { return byte != 0; })) {
                    as.error({}, "section '" + section.name + "' is NOBITS but holds data");
                    break;
                }
            }
        } else {
            entry.data = as.section_contents(SectionId{static_cast<std::uint32_t>(index)});
        }
        if (relocation_index[index] == 0) continue;
        auto& rel = output[relocation_index[index] - 1];
        rel.name = strings.add((as.object_.rela ? ".rela" : ".rel") + section.name);
        rel.type = as.object_.rela ? elf::sht_rela : elf::sht_rel;
        // LLVM MC marks a group member's relocations SHF_GROUP only.
        rel.flags = section.group ? elf::shf_group : elf::shf_info_link;
        rel.alignment = wide ? 8 : 4;
        rel.entry_size = wide ? (as.object_.rela ? 24 : 16) : (as.object_.rela ? 12 : 8);
        rel.link = symtab_index;
        rel.info = section_index[index];
        ByteWriter entries(big);
        for (const auto& relocation : relocations[index]) {
            const auto symbol = symbol_index[relocation.symbol.value];
            if (wide) {
                entries.u64(relocation.offset);
                if (as.object_.machine == 8) {
                    entries.u32(symbol);
                    entries.u8(0);
                    entries.u8(relocation.type >> 16U);
                    entries.u8(relocation.type >> 8U);
                    entries.u8(relocation.type);
                } else {
                    entries.u64((std::uint64_t{symbol} << 32U) | relocation.type);
                }
                if (as.object_.rela) entries.u64(static_cast<std::uint64_t>(relocation.addend));
            } else {
                entries.u32(relocation.offset);
                entries.u32((std::uint64_t{symbol} << 8U) | (relocation.type & 0xffU));
                if (as.object_.rela) entries.u32(static_cast<std::uint64_t>(relocation.addend));
            }
        }
        rel.data = std::move(entries.data());
    }
    auto& symtab_section = output[symtab_index - 1];
    symtab_section.name = strings.add(".symtab");
    symtab_section.type = elf::sht_symtab;
    symtab_section.alignment = wide ? 8 : 4;
    symtab_section.entry_size = wide ? 24 : 16;
    symtab_section.link = 1;
    symtab_section.info = first_global;
    symtab_section.data = std::move(symtab.data());
    strtab.name = strings.add(".strtab");
    strtab.data = strings.data();
    return write_elf(as.object_, as.target_.elf_flags(), output, 1);
}

std::optional<std::vector<std::uint8_t>> Assembler::run(
    const std::vector<const SourceFile*>& sources) {
    const auto errors = diagnostics_.errors();
    if (!options_.directives.empty()) {
        std::string text;
        for (const auto& line : options_.directives) text += line + '\n';
        const auto* command = sources_.add("<command line>", std::move(text));
        impl_->process_text(command->text, command, 0, 1, std::nullopt);
    }
    for (const auto* source : sources) {
        impl_->in_block_comment = false;
        impl_->process_text(source->text, source, 0, 1, std::nullopt);
    }
    if (impl_->capture) {
        error(impl_->capture->location, impl_->capture->kind == Impl::Capture::Kind::Macro
                                            ? "missing .endm" : "missing .endr");
    }
    if (!impl_->conditionals.empty()) error({}, "missing .endif at the end of the file");
    if (impl_->open_frame) {
        error(impl_->frames[*impl_->open_frame].location, "missing .cfi_endproc");
    }
    if (diagnostics_.errors() != errors) return std::nullopt;
    target_.flush(*this);
    impl_->pending_row.reset();
    target_.finish(*this);
    if (impl_->has_subsections) impl_->merge_subsections();
    if (diagnostics_.errors() != errors) return std::nullopt;
    if (!impl_->layout(true)) return std::nullopt;
    if (!impl_->frames.empty()) {
        if (impl_->eh_frame) impl_->emit_frames(true);
        if (impl_->debug_frame) impl_->emit_frames(false);
    }
    target_.finish_tables(*this);
    if (diagnostics_.errors() != errors) return std::nullopt;
    if (!impl_->layout(true)) return std::nullopt;
    impl_->emit_line_table();
    if (!impl_->layout(true)) return std::nullopt;
    if (object_.format == Format::MachO) impl_->section_addresses = macho_section_addresses(*this);
    if (!impl_->resolve_fixups() || diagnostics_.errors() != errors) return std::nullopt;
    auto object = impl_->write();
    if (diagnostics_.errors() != errors) return std::nullopt;
    return object;
}

std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, Target& target, const Options& options,
    SourceManager& manager, Diagnostics& diagnostics) {
    Assembler assembler(target, options, manager, diagnostics);
    return assembler.run(sources);
}

} // namespace cross::assembler
