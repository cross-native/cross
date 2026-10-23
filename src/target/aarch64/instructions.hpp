// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cross {
struct OperandType;
struct RelocatedSlice;
}

namespace cross::aarch64 {

// The instructions the AArch64 backend selects, which name the first
// entries of `TargetInfo::instruction_costs`.
enum class Instruction : std::uint16_t {
#define X(name, mnemonic, ...) name,
#include "target/aarch64/instructions.def"
#undef X
};

// The operand types of AArch64 `instruction` model entries, by
// OperandType::id.
enum class OperandTypeId : std::uint16_t {
    // General registers: x0-x30 or w0-w30 with the zero register or with sp.
    X,
    W,
    XSp,
    WSp,
    // Views of the SIMD and floating-point registers.
    B,
    H,
    S,
    D,
    Q,
    Immediate,
    // Encoded immediates.
    Bitmask32,
    Bitmask64,
    Fp8,
    FpZero,
    AddImmediate,
    AddImmediateNegated,
    Wide32,
    Wide64,
    WideInverted32,
    WideInverted64,
    Move32,
    Move64,
    // Shifted and extended register modifiers.
    Shift32,
    Shift64,
    ShiftRotate32,
    ShiftRotate64,
    Extend32,
    Extend64W,
    Extend64X,
    // Named values.
    Condition,
    InvertedCondition,
    SystemRegister,
    PstateField,
    Barrier,
    Prefetch,
    // Memory operands.
    Memory,
    MemoryScaled,
    MemoryUnscaled,
    MemoryPre,
    MemoryPost,
    MemoryRegister,
    Pair,
    PairPre,
    PairPost,
    // PC-relative labels.
    Branch26,
    Call26,
    Branch19,
    Branch14,
    Literal19,
    Adr21,
    Page21,
};

// The operand type that AArch64 instruction entries name `name`.
[[nodiscard]] std::optional<OperandType> operand_type(std::string_view name);

// Where the word holds the relocated value of an operand type: of a label,
// an add or move immediate, or a scaled memory offset; none for others.
[[nodiscard]] std::span<const RelocatedSlice> relocated_slices(OperandTypeId type);

} // namespace cross::aarch64
