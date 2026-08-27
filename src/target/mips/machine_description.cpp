// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/mips/machine_description.hpp"

#include <array>

namespace cross::mips {
namespace {

constexpr auto descriptors = std::array{
    OpcodeDescriptor{Opcode::None, {}},
#define X(name, spelling) OpcodeDescriptor{Opcode::name, spelling},
#include "target/mips/machine_opcodes.def"
#undef X
};

static_assert(descriptors.size() == static_cast<std::size_t>(Opcode::Count));

} // namespace

Opcode decode_opcode(machine::TargetOpcodeId opcode) noexcept {
    if (opcode.value >= static_cast<std::uint32_t>(Opcode::Count)) {
        return Opcode::None;
    }
    return static_cast<Opcode>(opcode.value);
}

std::optional<Opcode> find_opcode(std::string_view name) noexcept {
    for (const auto& descriptor : descriptors) {
        if (!descriptor.name.empty() && descriptor.name == name) {
            return descriptor.opcode;
        }
    }
    return std::nullopt;
}

std::string_view opcode_name(machine::TargetOpcodeId opcode) noexcept {
    return descriptors[static_cast<std::size_t>(decode_opcode(opcode))].name;
}

const OpcodeDescriptor* describe_opcode(
    machine::TargetOpcodeId opcode) noexcept {
    const auto decoded = decode_opcode(opcode);
    return decoded == Opcode::None
               ? nullptr
               : &descriptors[static_cast<std::size_t>(decoded)];
}

std::span<const OpcodeDescriptor> opcode_descriptors() noexcept {
    return descriptors;
}

} // namespace cross::mips
