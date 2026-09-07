// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cross::x86_64 {

inline constexpr machine::PhysicalRegisterId flags_storage{64};

// This enumeration is local to the x86-64 target.  Machine IR carries only
// TargetOpcodeId, so adding another architecture does not grow a global
// opcode enumeration or introduce architecture checks into the middle end.
enum class Opcode : std::uint16_t {
    None,
#define X(name, spelling) name,
#include "target/x86_64/machine_opcodes.def"
#undef X
    Count,
};

enum class OpcodeProperty : std::uint32_t {
    None = 0,
    Comparison = 1U << 0U,
    Test = 1U << 1U,
    Floating = 1U << 2U,
    Vector = 1U << 3U,
    Reduction = 1U << 4U,
    Atomic = 1U << 5U,
    Stack = 1U << 6U,
    Aggregate = 1U << 7U,
    Variadic = 1U << 8U,
    Immediate = 1U << 9U,
    Memory = 1U << 10U,
};

constexpr OpcodeProperty operator|(OpcodeProperty left,
                                   OpcodeProperty right) {
    return static_cast<OpcodeProperty>(
        static_cast<std::uint32_t>(left) |
        static_cast<std::uint32_t>(right));
}

struct OpcodeDescriptor {
    Opcode opcode{Opcode::None};
    std::string_view name;
    OpcodeProperty properties{OpcodeProperty::None};
};

[[nodiscard]] constexpr machine::TargetOpcodeId machine_opcode(
    Opcode opcode) {
    return {static_cast<std::uint32_t>(opcode)};
}

[[nodiscard]] Opcode decode_opcode(machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] std::optional<Opcode> find_opcode(
    std::string_view name) noexcept;
[[nodiscard]] std::string_view opcode_name(
    machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] const OpcodeDescriptor* describe_opcode(
    machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] std::span<const OpcodeDescriptor> opcode_descriptors() noexcept;
[[nodiscard]] bool has_property(machine::TargetOpcodeId opcode,
                                OpcodeProperty property) noexcept;

// Form relations replace suffix manipulation in instruction combining.
[[nodiscard]] machine::TargetOpcodeId base_opcode(
    machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] machine::TargetOpcodeId immediate_opcode(
    machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] machine::TargetOpcodeId memory_opcode(
    machine::TargetOpcodeId opcode) noexcept;

} // namespace cross::x86_64
