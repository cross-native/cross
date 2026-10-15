// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace cross::native {

// The call-frame rule that .cfi directives describe at one point of a
// function: the CFA as a register plus an offset, the CFA's distance from
// the stack pointer and from a frame register, and the CFA-relative homes of
// saved registers. Registers are DWARF numbers.
struct CfiState {
    unsigned cfa_register{};
    std::int64_t cfa_offset{};
    std::int64_t stack_offset{};
    std::optional<std::pair<unsigned, std::int64_t>> frame_register;
    std::vector<std::pair<unsigned, std::int64_t>> saves;
};

// Describes frame programs, whose instructions carry typed frame effects,
// with .cfi directives. The target names its stack pointer, the CFA's
// distance from it at entry, and the DWARF number and assembler spelling of
// its registers.
class FrameCfi {
public:
    FrameCfi(unsigned stack_pointer, std::int64_t entry_offset,
             std::function<unsigned(machine::Register)> number,
             std::function<std::string(unsigned)> spelling)
        : stack_pointer_(stack_pointer), entry_offset_(entry_offset),
          number_(std::move(number)), spelling_(std::move(spelling)) {}

    [[nodiscard]] CfiState entry() const;
    // The state after a frame-program instruction with `effect`.
    [[nodiscard]] CfiState after(CfiState state,
                                 const machine::FrameEffect& effect) const;
    // Prints the directives that change `current` into `target`.
    void transition(std::ostream& output, CfiState& current,
                    const CfiState& target) const;
    // Prints the directive that marks the incoming value of `reg` lost, as
    // in a function that never returns and skips its incoming saves.
    void undefined(std::ostream& output, unsigned reg) const;

private:
    unsigned stack_pointer_;
    std::int64_t entry_offset_;
    std::function<unsigned(machine::Register)> number_;
    std::function<std::string(unsigned)> spelling_;
};

} // namespace cross::native
