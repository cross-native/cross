// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cross::mips {

enum class Opcode : std::uint16_t {
    None,
#define X(name, spelling) name,
#include "target/mips/machine_opcodes.def"
#undef X
    Count,
};

struct OpcodeDescriptor {
    Opcode opcode{Opcode::None};
    std::string_view name;
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

} // namespace cross::mips
