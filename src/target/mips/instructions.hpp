// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cross::mips {

// The instructions the MIPS backend selects, which name the first entries
// of `TargetInfo::instruction_costs`.
enum class Instruction : std::uint16_t {
#define X(name, mnemonic, ...) name,
#include "target/mips/instructions.def"
#undef X
};

// The operand types of MIPS `instruction` model entries, by
// OperandType::id.
enum class OperandTypeId : std::uint16_t {
    Gpr,
    Fpr32,
    Fpr64,
    Cpr,
    Cpr64,
    Fcc,
    Cp0,
    // A hardware register of RDHWR.
    Hwr,
    Fcr,
    Immediate,
    Expression,
    Memory,
    Branch,
    Jump,
};

} // namespace cross::mips
