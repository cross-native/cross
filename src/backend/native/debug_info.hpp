// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/options.hpp"
#include "common/source.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"
#include "target/subtarget.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cross::native {

// Where `location` is written: a generated token at the outermost invocation
// that produced it, preprocessed text at the file and line its line marker
// names.
[[nodiscard]] SourceLocation written_location(SourceLocation location);

// The DWARF description of one compilation group, written as assembler
// directives. Target emitters print line rows and call-frame directives and
// record each function they emit; finish() adds the file table before the
// assembly and the debugging sections after it.
class DebugInfo {
public:
    DebugInfo(const CompilerOptions& options, const hir::Module& module,
              const mir::ManagedModule& managed,
              std::span<const EnumDecl> enumerations,
              const Subtarget& subtarget);

    // Whether -g selected an entry, and the contents it asks for.
    [[nodiscard]] bool active() const { return options_.debug_info.has_value(); }
    [[nodiscard]] bool lines() const { return active() && options_.debug_info->lines; }
    [[nodiscard]] bool frames() const { return active() && options_.debug_info->frames; }
    [[nodiscard]] bool variables() const {
        return active() && options_.debug_info->variables;
    }

    // Starts `function`: its first row is printed even if it repeats the
    // last row of the previous function.
    void begin_function(hir::FunctionId function);
    // The next row that does not copy a parameter is the first after the
    // prologue.
    void end_prologue() { prologue_end_ = true; }
    // Prints the .loc row of `location` before an instruction unless the
    // location is unknown or repeats the previous row and, where the
    // instruction comes from other inlined calls than the previous one
    // (`site` is the innermost, in the MIR function being emitted), a label
    // that ends and starts their code.
    void row(std::ostream& output, SourceLocation location,
             std::optional<mir::InlineSiteId> site = std::nullopt);
    // Where MIR slot `slot`, named `name`, of the function being emitted
    // lives in all of its body: at DWARF register `reg` plus `offset`, or in
    // that register.
    void slot_home(std::uint32_t slot, std::string name, unsigned reg,
                   std::int64_t offset, bool in_register = false);
    // Where the value of `in` parameter `index` lives in all of the body.
    void parameter_home(std::uint64_t index, unsigned reg, std::int64_t offset);
    // The call-frame information of the function being emitted is exact at
    // every instruction, and in its body the CFA is DWARF register `reg`
    // plus `offset`: homes based on `reg` are described from the CFA.
    void frame_base(unsigned reg, std::int64_t offset) { base_ = {reg, offset}; }
    // Records a function emitted from `symbol` (its assembler spelling) up to
    // the returned label, which the emitter defines after the function, and
    // whether .cfi directives describe its frame.
    [[nodiscard]] std::string end_function(hir::FunctionId function,
                                           std::string symbol, bool frame);

    // The assembly with the file table and frame-section selection before it
    // and the DWARF sections after it.
    [[nodiscard]] std::string finish(std::string assembly);

private:
    struct Position {
        unsigned file{};
        unsigned line{};
        unsigned column{};
        friend bool operator==(const Position&, const Position&) = default;
    };
    struct Home {
        std::string name;
        unsigned reg{};
        std::int64_t offset{};
        bool in_register{};
    };
    // The code between two labels.
    struct Range {
        std::string begin;
        std::string end;
    };
    struct Function {
        hir::FunctionId source;
        std::string symbol;
        std::string end_label;
        bool frame{};
        std::unordered_map<std::uint32_t, Home> homes;
        std::unordered_map<std::uint64_t, Home> parameter_homes;
        std::optional<std::pair<unsigned, std::int64_t>> base;
        // The code of each inline site of the MIR function.
        std::vector<std::vector<Range>> sites;
    };

    [[nodiscard]] std::optional<Position> position(SourceLocation location);
    [[nodiscard]] unsigned file_number(const std::string& path);
    // Prints and returns a new label in the function being emitted.
    std::string place_label(std::ostream& output);

    const CompilerOptions& options_;
    const hir::Module& module_;
    const mir::ManagedModule& managed_;
    std::span<const EnumDecl> enumerations_;
    const Subtarget& subtarget_;
    ObjectFormat format_;
    // The first input, `.file 0`, and the other paths after
    // -ffile-prefix-map; index N is `.file N + 1`.
    std::string unit_;
    std::vector<std::string> files_;
    std::unordered_map<std::string, unsigned> file_numbers_;
    std::vector<Function> functions_;
    // The function being emitted: its homes, frame base, label stem and
    // label count, MIR body, the inline sites around the code printed last
    // (outermost first), and the code of each site.
    std::unordered_map<std::uint32_t, Home> homes_;
    std::unordered_map<std::uint64_t, Home> parameter_homes_;
    std::optional<std::pair<unsigned, std::int64_t>> base_;
    std::string stem_;
    std::size_t next_label_{};
    const mir::ManagedFunction* body_{};
    std::vector<std::uint32_t> open_sites_;
    std::vector<std::vector<Range>> site_ranges_;
    std::optional<Position> last_row_;
    // The written positions of the parameters of the function being emitted.
    std::vector<Position> parameters_;
    bool prologue_end_{};
    bool rows_{};
};

} // namespace cross::native
