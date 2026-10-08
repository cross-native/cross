// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "target/target.hpp"
#include "common/relocation_addend.hpp"

namespace cross {

// Registry-owned source-form and address-representability checking, shared with
// target lowering. Allocation, emission and raw control-flow resource proofs
// remain lowering responsibilities.
std::optional<std::string> instruction_source_error(const TargetInfo&, const Subtarget&,
    std::string_view name, std::span<const EvaluationInstructionOperand>);

bool instruction_immediate_fits(std::uint64_t value, const InstructionOperandEntry&);

inline bool instruction_label_accepts(const InstructionOperandEntry& field, bool same_function) {
    return field.allow_label &&
        (field.label_scope == InstructionLabelScope::AnyVisible || same_function);
}

struct InstructionAddressSelection {
    std::string error;
    unsigned scale{1};
    RelocationAddend displacement{};
    bool deferred{};
};
InstructionAddressSelection select_instruction_address(const TargetInfo&, const Subtarget&,
    const EvaluationInstructionOperand::Memory&);

} // namespace cross
