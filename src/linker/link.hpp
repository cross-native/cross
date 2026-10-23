// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "linker/input.hpp"
#include "linker/linker.hpp"
#include "linker/script.hpp"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross::linker {

// A gap in an output section that its fill pattern fills.
struct Padding {
    std::uint64_t offset{};
    std::uint64_t size{};
    // Zeros when null.
    const script::Fill* fill{};
};

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
    // Whether input sections, data, assignments, or fills were placed in
    // the section (GNU ld creates it then), and whether it had contents on
    // the last layout pass.
    bool created{};
    bool emitted{};
    // An assignment to a symbol, which keeps an empty section's address,
    // and what keeps an empty section (GNU ld's SEC_KEEP and early sizing):
    // a KEEP input section, an assignment to `.`, or contents before
    // .eh_frame and .pdr editing.
    bool symbols{};
    bool keep{};
    // Whether an assignment advanced `.`, which allocates a section of
    // assignments alone.
    bool advanced{};
    // The region attributes (script::region_*) of the contents.
    std::uint8_t attributes{};
    // The AT> region, carried from a previous section of the same region;
    // an orphan's regions, from the section it follows.
    std::string load_region;
    std::string region;
    std::string explicit_load_region;
    // The fill gaps of the last layout pass, and the region it used.
    std::vector<Padding> padding;
    std::size_t region_index{};
};

// A CIE or FDE of an .eh_frame input section that the linker edits (GNU
// ld's eh_cie_fde).
struct FrameEntry {
    enum class Kind : std::uint8_t { Cie, Fde, Terminator };
    Kind kind{};
    std::uint64_t offset{};
    // The size without trailing DW_CFA_nop padding.
    std::uint64_t size{};
    // The entry's relocations: [first, end) of the section's relocations,
    // and an FDE's for its initial location.
    std::size_t first_relocation{};
    std::size_t end_relocation{};
    std::size_t pc_relocation{};
    std::uint8_t fde_encoding{};
    bool removed{true};
    std::uint64_t new_offset{};
    // An FDE: its CIE in the section (none for an empty address range) and,
    // once kept, the CIE kept for it and that CIE's new offset.
    std::optional<std::size_t> cie;
    const InputSection* kept_section{};
    std::uint64_t kept_offset{};
    // A CIE: what decides whether it merges with another (empty when it
    // cannot), whether its personality pointer is 8-byte aligned, and the
    // CIE it merged with.
    std::string key;
    bool aligned8{};
    const InputSection* merged_section{};
    std::size_t merged_index{};
};

struct FrameSection {
    std::vector<FrameEntry> entries;
    std::uint64_t original_size{};
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

// Whether an input section description matches an input section.
[[nodiscard]] bool matches(const script::InputPattern& pattern, const InputSection& section);

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
    [[nodiscard]] std::optional<std::uint64_t> tls_base() const;
    [[nodiscard]] std::uint64_t tls_alignment() const;
    [[nodiscard]] bool position_independent() const { return options_.pie; }
    // Records a relative dynamic relocation of a field in a section.
    void add_relative(const InputSection& section, std::uint64_t offset, std::uint64_t value,
                      std::string_view what);
    // The relocation view of an included section.
    [[nodiscard]] std::unique_ptr<RelocationContext> context(const InputSection& section);
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
    std::optional<std::string> find_script_input(const script::ScriptInput& input);
    void require(const std::string& name);
    bool check_cross_references();
    bool add_file(std::unique_ptr<InputFile> file);
    void add_symbols(InputFile& file);
    Symbol& symbol(const std::string& name);
    [[nodiscard]] Symbol* find(const std::string& name) const;
    void reference(Symbol& symbol, bool weak, InputFile* file);
    void fetch_pending();
    void define_script_symbols();
    void allocate_commons();
    void collect_garbage();
    [[nodiscard]] bool gc_root_defined() const;
    void merge_sections();
    void prune_records();
    bool check_undefined();
    // Position-independent executables (dynamic.cpp).
    void create_dynamic_sections();
    void size_dynamic_relocations(std::size_t count);
    void write_dynamic_sections();
    // Range-extension thunks: places a section after an included one.
    void insert_section(InputSection& after, std::unique_ptr<InputSection> section);
    void report_duplicate(const Symbol& existing, const InputFile& file, const FileSymbol& symbol);
    [[nodiscard]] std::vector<InputSection*> included_sections() const;
    [[nodiscard]] InputSection* defining_section(const InputFile& file, std::uint32_t index) const;

    // Unwind tables (eh_frame.cpp).
    void parse_frames();
    bool parse_frame_section(InputSection& section);
    void mark_frames(InputSection& section, const std::function<void(InputSection*)>& mark);
    void edit_frames(bool relocatable);
    void create_frame_header();
    void write_frame_header(OutputSection& output);
    void patch_frame_pointers(OutputSection& output);

    // Placement and addresses (layout.cpp).
    bool place_sections();
    void place_orphans(std::vector<InputSection*>& orphans);
    bool layout();
    bool layout_pass(bool final);
    void lay_out_section(Placement& placement, bool final);
    // An expression value: a plain number, an absolute address, or an
    // address in an output section (GNU ld's three kinds; inside an output
    // section a plain number assigned to a symbol is an offset in it).
    struct Value {
        enum class Kind : std::uint8_t { Number, Absolute, Relative };
        std::uint64_t value{};
        Kind kind{Kind::Number};
        OutputSection* section{};
    };
    std::optional<Value> evaluate(const script::Expression& expression);
    // GNU ld's number for a value: a relative one is an offset in its
    // section.
    [[nodiscard]] static std::uint64_t raw(const Value& value) {
        return value.kind == Value::Kind::Relative && value.section ? value.value - value.section->address
                                                                     : value.value;
    }
    // A binary operator, or ALIGN, MAX, or MIN, applied to two values.
    std::optional<Value> combine(std::string_view op, const Value& left, const Value& right,
                                 const script::Expression& where);
    [[nodiscard]] Value location_counter();
    void assign(const script::Assignment& assignment, OutputSection* section, bool final);
    [[nodiscard]] std::uint64_t assigned_value(const script::Assignment& assignment) const;
    [[nodiscard]] OutputSection* output_named(std::string_view name) const;
    [[nodiscard]] OutputSection* section_for_dot() const;
    [[nodiscard]] const OutputSection* nearby_section(const OutputSection& section, std::uint64_t address) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> fill_pattern(const script::Fill& fill);
    [[nodiscard]] std::size_t region_index(std::string_view name) const;
    [[nodiscard]] std::size_t default_region(const Placement& placement) const;
    void resolve_inserts();
    void compute_output_attributes();
    void propagate_load_regions();
    bool build_segments();
    void assign_offsets();
    [[nodiscard]] std::uint64_t headers_size() const;
    void expression_error(const script::Expression& expression, std::string_view message);

    // Contents and files (output.cpp).
    [[nodiscard]] bool contributes(const InputFile& file) const;
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
    // Symbols the script reads, which garbage collection keeps, and the
    // symbols that relocations of live sections refer to after it.
    std::set<std::string> script_references_;
    std::unordered_set<const Symbol*> live_references_;
    std::vector<std::unique_ptr<OutputSection>> outputs_;
    std::vector<std::unique_ptr<Placement>> placements_;
    std::vector<Step> steps_;
    std::vector<Segment> segments_;
    // Parsed .eh_frame sections, the FDEs of each code section, whether the
    // .eh_frame_hdr table can be built, and the .eh_frame_hdr section.
    std::unordered_map<const InputSection*, FrameSection> frames_;
    std::unordered_map<const InputSection*, std::vector<std::pair<InputSection*, std::size_t>>> fdes_;
    bool frame_table_{true};
    InputSection* frame_header_{};
    // A position-independent executable's dynamic sections, the relative
    // relocations they hold, and how many the layout made room for.
    InputSection* dynamic_{};
    InputSection* dynamic_relocations_{};
    InputSection* dynamic_symbols_{};
    InputSection* dynamic_strings_{};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> relatives_;
    std::size_t relative_count_{};

    // Layout pass state.
    std::uint64_t dot_{};
    Placement* current_{};
    std::uint64_t section_base_{};
    bool uses_headers_size_{};
    bool final_{};
    bool expression_failed_{};
    // Whether the expression being evaluated read the location counter
    // outside an output section (GNU ld's rel_from_abs).
    bool from_dot_{};
    std::unordered_set<std::string> assigned_;
    // Memory regions in MEMORY order, then the default region: each one's
    // next address and the address, load address, and size of the last
    // section whose load address the following sections keep pace with.
    struct RegionState {
        std::uint64_t current{};
        bool has_last{};
        std::uint64_t last_address{};
        std::uint64_t last_load{};
        std::uint64_t last_size{};
        // The start and end of the run of TLS NOBITS sections that ends the
        // region: `.` stays at its start, and each such section follows the
        // previous one in the TLS block.
        std::optional<std::pair<std::uint64_t, std::uint64_t>> tbss_run;
    };
    std::vector<RegionState> regions_;
    // GNU ld's choice of a section for a symbol assigned from `.` outside
    // output sections: the step being evaluated, the last allocated output
    // section entered, whether `.` was assigned since, and whether `_end`
    // was assigned before each step.
    std::size_t step_index_{};
    const Placement* current_section_{};
    bool prefer_next_{};
    bool found_end_{};
    std::vector<bool> after_end_;
    // DATA_SEGMENT_ALIGN: whether the page-saving form was chosen, and what
    // the current pass saw.
    enum class SegmentPhase : std::uint8_t { None, AlignSeen, EndSeen };
    bool segment_adjust_{};
    SegmentPhase segment_phase_{};
    std::uint64_t segment_base_{};
    std::uint64_t segment_end_{};
    std::uint64_t segment_common_page_{};
    std::size_t segment_estimate_{};
};

} // namespace cross::linker
