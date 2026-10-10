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
#include <vector>

namespace cross::native {

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

    // Starts a function: its first row is printed even if it repeats the
    // last row of the previous function.
    void begin_function();
    // The next row is the first after the prologue.
    void end_prologue() { prologue_end_ = true; }
    // Prints the .loc row of `location` before an instruction unless the
    // location is unknown or repeats the previous row.
    void row(std::ostream& output, SourceLocation location);
    // Where MIR slot `slot`, named `name`, of the function being emitted
    // lives in all of its body: at DWARF register `reg` plus `offset`, or in
    // that register.
    void slot_home(std::uint32_t slot, std::string name, unsigned reg,
                   std::int64_t offset, bool in_register = false);
    // Where the value of `in` parameter `index` lives in all of the body.
    void parameter_home(std::uint64_t index, unsigned reg, std::int64_t offset);
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
    struct Function {
        hir::FunctionId source;
        std::string symbol;
        std::string end_label;
        bool frame{};
        std::unordered_map<std::uint32_t, Home> homes;
        std::unordered_map<std::uint64_t, Home> parameter_homes;
    };

    [[nodiscard]] std::optional<Position> position(SourceLocation location);
    [[nodiscard]] unsigned file_number(const std::string& path);

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
    std::unordered_map<std::uint32_t, Home> homes_;
    std::unordered_map<std::uint64_t, Home> parameter_homes_;
    std::optional<Position> last_row_;
    bool prologue_end_{};
    bool rows_{};
};

} // namespace cross::native
