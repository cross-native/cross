// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "linker/input.hpp"
#include "linker/linker.hpp"
#include "linker/script.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::linker {

// One output section statement in layout order, from the script or for
// orphan input sections.
struct Placement {
    const script::OutputSection* statement{};
    // Null for /DISCARD/.
    OutputSection* output{};
    struct Item {
        // Null for orphan input sections.
        const script::SectionItem* item{};
        std::vector<InputSection*> sections;
        // A data command's offset within the output section; its value,
        // or an assignment's.
        std::uint64_t offset{};
        std::uint64_t value{};
    };
    std::vector<Item> items;
    // Whether the section had content on the last layout pass.
    bool emitted{};
};

struct Step {
    const script::Command* command{};
    Placement* placement{};
    // An assignment's value on the last layout pass, for the map.
    std::uint64_t value{};
};

struct Segment {
    std::uint32_t type{};
    std::uint32_t flags{};
    std::vector<OutputSection*> sections;
    bool file_header{};
    bool program_headers{};
    std::optional<std::uint64_t> physical;
    std::uint64_t offset{};
    std::uint64_t address{};
    std::uint64_t physical_address{};
    std::uint64_t file_size{};
    std::uint64_t memory_size{};
    std::uint64_t alignment{1};
};

class Link {
public:
    Link(const Options& options, TargetLookup lookup, Diagnostics& diagnostics)
        : options_(options), lookup_(lookup), diagnostics_(diagnostics) {}
    bool run();

    // Relocation support.
    [[nodiscard]] SymbolValue symbol_value(const InputFile& file, std::uint32_t index) const;
    [[nodiscard]] std::uint64_t address(const InputSection& section) const {
        return section.output ? section.output->address + section.offset : 0;
    }
    // The address of an offset within an input section, merged or not.
    [[nodiscard]] std::uint64_t section_address(const InputSection& section,
                                                std::uint64_t offset) const;
    [[nodiscard]] std::optional<std::uint64_t> gp() const;
    // Whether a relocation's symbol is code that `file` does not contribute.
    [[nodiscard]] bool foreign(const InputFile& file, std::uint32_t index) const;
    [[nodiscard]] std::uint64_t mask(std::uint64_t value) const {
        return object_.elf64 ? value : value & 0xffffffffU;
    }
    Diagnostics& diagnostics() { return diagnostics_; }

private:
    // Inputs and symbols (link.cpp).
    bool read_inputs();
    bool open_input(const Input& input, std::vector<std::uint8_t> bytes, const std::string& path);
    std::optional<std::string> find_library(const std::string& name);
    bool add_file(std::unique_ptr<InputFile> file);
    void add_symbols(InputFile& file);
    Symbol& symbol(const std::string& name);
    [[nodiscard]] Symbol* find(const std::string& name) const;
    void reference(Symbol& symbol, bool weak, InputFile* file);
    void fetch_pending();
    void define_script_symbols();
    void allocate_commons();
    void collect_garbage();
    void merge_sections();
    void prune_records();
    bool check_undefined();
    void report_duplicate(const Symbol& existing, const InputFile& file, const FileSymbol& symbol);
    [[nodiscard]] std::vector<InputSection*> included_sections() const;
    [[nodiscard]] InputSection* defining_section(const InputFile& file, std::uint32_t index) const;

    // Placement and addresses (layout.cpp).
    bool place_sections();
    void place_orphans(std::vector<InputSection*>& orphans);
    bool layout();
    bool layout_pass(bool final);
    void lay_out_section(Placement& placement, bool final);
    struct Value {
        std::uint64_t value{};
        // Derived from the location counter or a section-relative symbol,
        // and the output section it belongs to.
        bool relative{};
        OutputSection* section{};
    };
    std::optional<Value> evaluate(const script::Expression& expression);
    void assign(const script::Assignment& assignment, OutputSection* section, bool final);
    [[nodiscard]] std::uint64_t assigned_value(const script::Assignment& assignment) const;
    [[nodiscard]] OutputSection* output_named(std::string_view name) const;
    bool build_segments();
    void assign_offsets();
    [[nodiscard]] std::uint64_t headers_size() const;
    void expression_error(const script::Expression& expression, std::string_view message);

    // Contents and files (output.cpp).
    bool write_contents();
    bool write_executable();
    bool write_relocatable();
    void write_map() const;
    std::uint64_t entry_address();

    const Options& options_;
    TargetLookup lookup_;
    Diagnostics& diagnostics_;
    SourceManager sources_;
    script::Script script_;
    std::unique_ptr<Target> target_;
    assembler::ObjectInfo object_{};
    std::uint64_t page_size_{};
    std::vector<std::unique_ptr<InputFile>> files_;
    std::vector<Archive> archives_;
    // Archive members (archive, member) that undefined references need.
    std::vector<std::pair<std::size_t, std::size_t>> pending_;
    bool fetching_{};
    std::deque<Symbol> symbols_;
    std::unordered_map<std::string, Symbol*> symbol_index_;
    std::unordered_set<std::string> comdat_signatures_;
    // Symbols the script reads, which garbage collection keeps.
    std::set<std::string> script_references_;
    std::vector<std::unique_ptr<OutputSection>> outputs_;
    std::vector<std::unique_ptr<Placement>> placements_;
    std::vector<Step> steps_;
    std::vector<Segment> segments_;

    // Layout pass state.
    std::uint64_t dot_{};
    Placement* current_{};
    std::uint64_t section_base_{};
    bool uses_headers_size_{};
    OutputSection* last_allocated_{};
    bool final_{};
    bool expression_failed_{};
    std::unordered_set<std::string> assigned_;
    // Memory regions: each one's next address, the load-minus-virtual
    // address of its last section (the default region is ""), and the
    // regions of the previous output section.
    std::map<std::string, std::uint64_t> region_cursors_;
    std::map<std::string, std::uint64_t> region_deltas_;
    std::string previous_region_;
    std::string previous_load_region_;
    std::size_t segment_estimate_{};
};

} // namespace cross::linker
