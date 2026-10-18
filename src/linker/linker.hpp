// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "assembler/assembler.hpp"
#include "common/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cross::linker {

// A static ELF linker: relocatable objects, archives, raw binary files, and
// symbol files in; an executable or a relocatable object out, laid out by a
// GNU ld linker script. Relocation semantics, header flags, and special
// sections belong to a Target.

namespace elf = assembler::elf;

struct InputFile;
struct OutputSection;
struct Symbol;

// Output format choices beyond the class and byte order: GNU ld's
// "traditional" formats (the `ts` emulations and `trad` formats).
struct OutputFormat {
    bool traditional{};
};

struct InputSection {
    InputFile* file{};
    std::string name;
    std::uint32_t type{elf::sht_progbits};
    std::uint64_t flags{};
    std::uint64_t alignment{1};
    std::uint64_t entry_size{};
    std::vector<std::uint8_t> data;
    std::uint64_t size{};
    // The relocations that apply to this section, in file order. MIPS ELF64
    // packs up to three types into `type`, the first in the low byte.
    std::vector<assembler::Relocation> relocations;
    bool rela{};
    // Placement.
    OutputSection* output{};
    std::uint64_t offset{};
    bool live{true};
    // A COMDAT duplicate, a /DISCARD/ match, or a section a target merged
    // into another.
    bool discarded{};
    bool keep{};
    std::optional<std::uint32_t> group;
    // A merged SHF_MERGE section: the group's first section, which holds
    // the merged contents, and each piece's input offset and offset there.
    InputSection* merged{};
    // A discarded section whose contents a target merged into this one.
    InputSection* combined{};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> pieces;

    [[nodiscard]] bool nobits() const { return type == elf::sht_nobits; }
    [[nodiscard]] bool alloc() const { return (flags & elf::shf_alloc) != 0; }
    [[nodiscard]] bool included() const { return live && !discarded; }
    // "file:(section+0xOFFSET)", for diagnostics.
    [[nodiscard]] std::string location(std::uint64_t offset) const;
};

struct FileSymbol {
    std::string name;
    std::uint64_t value{};
    std::uint64_t size{};
    std::uint8_t type{};
    std::uint8_t binding{};
    std::uint8_t other{};
    std::uint32_t shndx{};
    // The global symbol of a non-local file symbol.
    Symbol* global{};
};

struct Group {
    std::string signature;
    // The signature's index in the file's symbol table.
    std::uint32_t symbol{};
    bool comdat{};
    std::vector<std::uint32_t> members;
};

struct InputFile {
    // The path as given, and for an archive member the member name.
    std::string path;
    std::string member;
    assembler::ObjectInfo object;
    std::uint32_t flags{};
    // Indexed like the file's section headers; null where no section is
    // linked (string tables, relocations, groups).
    std::vector<std::unique_ptr<InputSection>> sections;
    std::vector<FileSymbol> symbols;
    std::vector<Group> groups;
    // A raw binary file, which has no header flags, and a file read only
    // for its symbol addresses.
    bool raw{};
    bool just_symbols{};
    // The file's .note.GNU-stack, and whether it asks for an executable
    // stack.
    bool stack_note{};
    bool executable_stack{};
    // MIPS: the gp value the object was assembled with.
    std::int64_t gp0{};
    [[nodiscard]] std::string name() const {
        return member.empty() ? path : path + "(" + member + ")";
    }
};

struct Symbol {
    enum class Kind : std::uint8_t { Undefined, Defined, Common, Lazy };
    std::string name;
    Kind kind{Kind::Undefined};
    bool weak{};
    // A definition in an input section, at `value` within it; absolute when
    // `section` is null and `output` is null.
    InputSection* section{};
    // A linker-script definition inside an output section: `value` is then
    // the absolute address and `output` gives the symbol's section index.
    // With `absolute`, the value counts as absolute in expressions and
    // `output` only gives the index (a symbol assigned from `.` outside
    // output sections).
    OutputSection* output{};
    bool absolute{};
    std::uint64_t value{};
    std::uint64_t size{};
    std::uint64_t common_alignment{};
    std::uint8_t type{elf::stt_notype};
    std::uint8_t other{};
    // The defining file, or for an undefined symbol the first referencing
    // one; for a lazy symbol the archive member's index.
    InputFile* file{};
    std::size_t archive{};
    std::size_t member{};
    // The input-section name of a common symbol's storage, such as COMMON,
    // and the section index the object gave it.
    std::string_view common_section;
    std::uint32_t common_index{};
    bool script{};
    bool referenced{};
    bool hidden{};
    [[nodiscard]] bool defined() const {
        return kind == Kind::Defined || kind == Kind::Common;
    }
};

struct OutputSection {
    std::string name;
    std::uint32_t type{elf::sht_progbits};
    std::uint64_t flags{};
    std::uint64_t alignment{1};
    std::uint64_t entry_size{};
    std::uint64_t address{};
    std::uint64_t load_address{};
    std::uint64_t size{};
    std::vector<InputSection*> sections;
    std::uint32_t index{};
    std::uint64_t offset{};
    std::vector<std::uint8_t> data;
    [[nodiscard]] bool nobits() const { return type == elf::sht_nobits; }
    [[nodiscard]] bool alloc() const { return (flags & elf::shf_alloc) != 0; }
};

// What a relocation sees of its symbol.
struct SymbolValue {
    std::uint64_t value{};
    std::string_view name;
    // An undefined weak symbol, which resolves to zero.
    bool undefined{};
    bool local{};
    bool section{};
    // A symbol in a removed section, referenced from a non-allocated one.
    bool removed{};
    // A section symbol of a merged section, whose addend RelocationContext::
    // merged maps.
    const InputSection* merge{};
};

class RelocationContext {
public:
    virtual ~RelocationContext() = default;
    [[nodiscard]] virtual SymbolValue symbol(std::uint32_t index) const = 0;
    // The address of an offset within the section being relocated.
    [[nodiscard]] virtual std::uint64_t place(std::uint64_t offset) const = 0;
    // _gp, or nullopt when nothing defines it.
    [[nodiscard]] virtual std::optional<std::uint64_t> gp() const = 0;
    // The start of the TLS segment, or nullopt without one.
    [[nodiscard]] virtual std::optional<std::uint64_t> tls_base() const = 0;
    // The address of an offset within a merged section.
    [[nodiscard]] virtual std::uint64_t merged(const InputSection& section,
                                               std::uint64_t offset) const = 0;
    [[nodiscard]] virtual const InputSection& section() const = 0;
    virtual void error(std::uint64_t offset, std::string_view message) const = 0;
    virtual void warning(std::uint64_t offset, std::string_view message) const = 0;
};

class Target {
public:
    virtual ~Target() = default;
    // Checks one input object against the output and merges its header
    // flags; false after a diagnostic.
    virtual bool add_object(const InputFile& file, Diagnostics& diagnostics) = 0;
    [[nodiscard]] virtual std::uint32_t output_flags() const = 0;
    [[nodiscard]] virtual std::uint64_t page_size() const = 0;
    // Special symbol sections, such as MIPS small common (SHN_MIPS_SCOMMON):
    // the input-section name of a common symbol with this index, or empty.
    [[nodiscard]] virtual std::string_view common_section(std::uint32_t shndx) const = 0;
    // Whether a section index names an undefined symbol.
    [[nodiscard]] virtual bool undefined_index(std::uint32_t shndx) const = 0;
    // Drops the records of a section that describe code its file does not
    // contribute, such as MIPS .pdr entries of discarded functions;
    // `foreign` tells whether a symbol is such code.
    virtual void prune(InputSection& section,
                       const std::function<bool(std::uint32_t symbol)>& foreign) const = 0;
    // Merges the target's register-usage and ABI sections of all live inputs
    // into the first of each kind, discarding the rest.
    virtual void merge_sections(std::span<InputSection* const> sections,
                                Diagnostics& diagnostics) = 0;
    // Records the final gp in the merged sections that carry it.
    virtual void finish_sections(std::optional<std::uint64_t> gp) = 0;
    // The program headers the target adds ahead of the loadable segments,
    // each with the output section it describes, in order.
    [[nodiscard]] virtual std::vector<std::pair<std::uint32_t, OutputSection*>> extra_segments(
        std::span<OutputSection* const> sections) const = 0;
    // Applies a section's relocations to `bytes`, its copy in the output.
    virtual void relocate(const RelocationContext& context,
                          std::span<std::uint8_t> bytes) const = 0;
    // Input sections the linker creates for the relocations of the included
    // sections, such as the MIPS GOT, before placement. Each names the file
    // it belongs to; the link keeps them.
    virtual std::vector<std::unique_ptr<InputSection>> create_sections(
        std::span<InputSection* const> /*sections*/) {
        return {};
    }
    // Symbols at the start of created sections, such as MIPS
    // _GLOBAL_OFFSET_TABLE_, which the link defines hidden unless an input
    // defines them.
    [[nodiscard]] virtual std::vector<std::pair<std::string, InputSection*>> created_symbols() const {
        return {};
    }
    // Sizes the created sections for the sections that remain included.
    virtual void size_sections(std::span<InputSection* const> /*sections*/) {}
    // Fills the created sections once addresses are final; `context` gives
    // the relocation view of an included section.
    virtual void fill_sections(
        std::span<InputSection* const> /*sections*/,
        const std::function<std::unique_ptr<RelocationContext>(const InputSection&)>& /*context*/) {}
    // A symbol the target defines for every link, such as MIPS _gp_disp,
    // which needs no definition and is left out of the symbol table.
    [[nodiscard]] virtual bool implicit_symbol(std::string_view /*name*/) const { return false; }
    // Whether the symbol table's sh_info counts the section symbols only,
    // not all local symbols (IRIX 5 conventions).
    [[nodiscard]] virtual bool sections_end_locals(const OutputFormat& format) const = 0;
    // Adjusts an output section's type, flags, and entry size to the
    // target's conventions for its name in the output format.
    virtual void finish_output_section(std::string_view name, const OutputFormat& format, std::uint32_t& type,
                                       std::uint64_t& flags, std::uint64_t& entry_size) const = 0;
    // The layout without a linker script, as script text; for a
    // relocatable link, the output sections that collect input sections
    // of given names, in order (the rest follow by name).
    [[nodiscard]] virtual std::string default_script(bool relocatable) const = 0;
    // Whether a local symbol's name marks an assembler label, which the
    // symbol table drops in mergeable sections, beyond `.L` and `..`.
    [[nodiscard]] virtual bool local_label(std::string_view name) const = 0;
    // The addends of a section's relocations, as a relocatable link keeps
    // them: RELA's, or REL's read from the section.
    [[nodiscard]] virtual std::vector<std::int64_t> addends(const InputSection& section) const = 0;
    // Writes a REL relocation's addend into its field; false when the field
    // cannot hold it.
    virtual bool store_addend(std::uint32_t type, std::int64_t addend,
                              std::span<std::uint8_t> field) const = 0;
};

using TargetFactory = std::unique_ptr<Target> (*)(const assembler::ObjectInfo& output);

struct Input {
    enum class Kind : std::uint8_t { File, Library, JustSymbols, Binary };
    Kind kind{Kind::File};
    std::string name;
};

struct Options {
    std::vector<Input> inputs;
    std::vector<std::filesystem::path> library_paths;
    std::optional<std::filesystem::path> script;
    // How many inputs precede the script, where its INPUT and GROUP files go.
    std::size_t script_position{};
    std::filesystem::path output{"a.out"};
    std::optional<std::string> entry;
    std::optional<std::filesystem::path> map;
    std::vector<std::string> undefined;
    // `--defsym NAME=EXPRESSION`, as script assignments.
    std::vector<std::string> definitions;
    // `-Ttext=ADDRESS` and `--section-start=NAME=ADDRESS`.
    std::vector<std::pair<std::string, std::uint64_t>> section_starts;
    std::optional<std::uint64_t> page_size;
    // Output format from `-m`, `--oformat`, `-EB`, and `-EL`; the first
    // object decides what these leave open.
    std::optional<bool> elf64;
    std::optional<bool> big_endian;
    OutputFormat format;
    bool gc_sections{};
    bool print_gc_sections{};
    // `--gc-keep-exported`: global symbols of default or protected
    // visibility keep their sections.
    bool keep_exported{};
    bool relocatable{};
    // `-d`: allocate common symbols in a relocatable link too.
    bool define_common{};
    // `--eh-frame-hdr`: a .eh_frame_hdr section and PT_GNU_EH_FRAME.
    bool eh_frame_header{};
    bool strip_all{};
    bool fatal_warnings{};
};

// Finds the target of an ELF machine number.
using TargetLookup = TargetFactory (*)(std::uint16_t machine);

// Links `options.inputs` into `options.output`; false after any error.
[[nodiscard]] bool link(const Options& options, TargetLookup lookup,
                        Diagnostics& diagnostics);

} // namespace cross::linker
