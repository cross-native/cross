// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/x86_64/machine_description.hpp"

#include <array>

namespace cross::x86_64 {
namespace {

constexpr OpcodeProperty properties(std::string_view name) {
    auto result = OpcodeProperty::None;
    if (name.starts_with("x86.cmp.") || name.starts_with("x86.fcmp.") ||
        name.starts_with("x86.vcmp.")) {
        result = result | OpcodeProperty::Comparison;
    }
    if (name.starts_with("x86.test.") || name.starts_with("x86.vtest.")) {
        result = result | OpcodeProperty::Test;
    }
    if (name.starts_with("x86.f") && !name.starts_with("x86.fixed.")) {
        result = result | OpcodeProperty::Floating;
    }
    if (name.starts_with("x86.v")) result = result | OpcodeProperty::Vector;
    if (name.starts_with("x86.vreduce.")) {
        result = result | OpcodeProperty::Reduction;
    }
    if (name.starts_with("x86.atomic.")) {
        result = result | OpcodeProperty::Atomic;
    }
    if (name.starts_with("x86.stack.")) {
        result = result | OpcodeProperty::Stack;
    }
    if (name.starts_with("x86.aggregate.")) {
        result = result | OpcodeProperty::Aggregate;
    }
    if (name.starts_with("x86.variadic.")) {
        result = result | OpcodeProperty::Variadic;
    }
    if (name.ends_with(".imm")) {
        result = result | OpcodeProperty::Immediate;
    }
    if (name.ends_with(".mem")) {
        result = result | OpcodeProperty::Memory;
    }
    return result;
}

constexpr auto descriptors = std::array{
    OpcodeDescriptor{Opcode::None, {}, OpcodeProperty::None},
#define X(name, spelling) \
    OpcodeDescriptor{Opcode::name, spelling, properties(spelling)},
#include "target/x86_64/machine_opcodes.def"
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
    const auto decoded = decode_opcode(opcode);
    return descriptors[static_cast<std::size_t>(decoded)].name;
}

const OpcodeDescriptor* describe_opcode(
    machine::TargetOpcodeId opcode) noexcept {
    const auto decoded = decode_opcode(opcode);
    if (decoded == Opcode::None) return nullptr;
    return &descriptors[static_cast<std::size_t>(decoded)];
}

std::span<const OpcodeDescriptor> opcode_descriptors() noexcept {
    return descriptors;
}

bool has_property(machine::TargetOpcodeId opcode,
                  OpcodeProperty property) noexcept {
    const auto* descriptor = describe_opcode(opcode);
    return descriptor &&
        (static_cast<std::uint32_t>(descriptor->properties) &
         static_cast<std::uint32_t>(property)) != 0;
}

machine::TargetOpcodeId base_opcode(
    machine::TargetOpcodeId opcode) noexcept {
    switch (decode_opcode(opcode)) {
    case Opcode::AddImm:
    case Opcode::AddMem: return Opcode::Add;
    case Opcode::SubImm:
    case Opcode::SubMem: return Opcode::Sub;
    case Opcode::MulImm:
    case Opcode::MulMem: return Opcode::Mul;
    case Opcode::AndImm:
    case Opcode::AndMem: return Opcode::And;
    case Opcode::OrImm:
    case Opcode::OrMem: return Opcode::Or;
    case Opcode::XorImm:
    case Opcode::XorMem: return Opcode::Xor;
    case Opcode::ShlImm: return Opcode::Shl;
    case Opcode::ShrSImm: return Opcode::ShrS;
    case Opcode::ShrUImm: return Opcode::ShrU;
    case Opcode::RotlImm: return Opcode::Rotl;
    case Opcode::RotrImm: return Opcode::Rotr;
    case Opcode::CmpEqImm: return Opcode::CmpEq;
    case Opcode::CmpNeImm: return Opcode::CmpNe;
    case Opcode::CmpSgeImm: return Opcode::CmpSge;
    case Opcode::CmpSgtImm: return Opcode::CmpSgt;
    case Opcode::CmpSleImm: return Opcode::CmpSle;
    case Opcode::CmpSltImm: return Opcode::CmpSlt;
    case Opcode::CmpUgeImm: return Opcode::CmpUge;
    case Opcode::CmpUgtImm: return Opcode::CmpUgt;
    case Opcode::CmpUleImm: return Opcode::CmpUle;
    case Opcode::CmpUltImm: return Opcode::CmpUlt;
    case Opcode::TestEqImm: return Opcode::TestEq;
    case Opcode::TestNeImm: return Opcode::TestNe;
    case Opcode::FaddMem: return Opcode::Fadd;
    case Opcode::FsubMem: return Opcode::Fsub;
    case Opcode::FmulMem: return Opcode::Fmul;
    case Opcode::FdivMem: return Opcode::Fdiv;
    default: return opcode;
    }
}

machine::TargetOpcodeId immediate_opcode(
    machine::TargetOpcodeId opcode) noexcept {
    switch (decode_opcode(base_opcode(opcode))) {
    case Opcode::Add: return Opcode::AddImm;
    case Opcode::Sub: return Opcode::SubImm;
    case Opcode::Mul: return Opcode::MulImm;
    case Opcode::And: return Opcode::AndImm;
    case Opcode::Or: return Opcode::OrImm;
    case Opcode::Xor: return Opcode::XorImm;
    case Opcode::Shl: return Opcode::ShlImm;
    case Opcode::ShrS: return Opcode::ShrSImm;
    case Opcode::ShrU: return Opcode::ShrUImm;
    case Opcode::Rotl: return Opcode::RotlImm;
    case Opcode::Rotr: return Opcode::RotrImm;
    case Opcode::CmpEq: return Opcode::CmpEqImm;
    case Opcode::CmpNe: return Opcode::CmpNeImm;
    case Opcode::CmpSge: return Opcode::CmpSgeImm;
    case Opcode::CmpSgt: return Opcode::CmpSgtImm;
    case Opcode::CmpSle: return Opcode::CmpSleImm;
    case Opcode::CmpSlt: return Opcode::CmpSltImm;
    case Opcode::CmpUge: return Opcode::CmpUgeImm;
    case Opcode::CmpUgt: return Opcode::CmpUgtImm;
    case Opcode::CmpUle: return Opcode::CmpUleImm;
    case Opcode::CmpUlt: return Opcode::CmpUltImm;
    default: return {};
    }
}

machine::TargetOpcodeId memory_opcode(
    machine::TargetOpcodeId opcode) noexcept {
    switch (decode_opcode(base_opcode(opcode))) {
    case Opcode::Add: return Opcode::AddMem;
    case Opcode::Sub: return Opcode::SubMem;
    case Opcode::Mul: return Opcode::MulMem;
    case Opcode::And: return Opcode::AndMem;
    case Opcode::Or: return Opcode::OrMem;
    case Opcode::Xor: return Opcode::XorMem;
    case Opcode::Fadd: return Opcode::FaddMem;
    case Opcode::Fsub: return Opcode::FsubMem;
    case Opcode::Fmul: return Opcode::FmulMem;
    case Opcode::Fdiv: return Opcode::FdivMem;
    default: return {};
    }
}

} // namespace cross::x86_64
