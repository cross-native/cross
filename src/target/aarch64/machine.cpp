// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/machine.hpp"

#include "target/operand_encoders.hpp"

#include <array>
#include <charconv>

namespace cross::aarch64 {
namespace {

constexpr auto names = std::array<std::string_view, static_cast<std::size_t>(Opcode::Count)>{
    std::string_view{},
#define X(name, spelling) std::string_view{spelling},
#include "target/aarch64/machine_opcodes.def"
#undef X
};

} // namespace

Opcode decode_opcode(machine::TargetOpcodeId opcode) noexcept {
    if (opcode.value >= static_cast<std::uint32_t>(Opcode::Count)) return Opcode::None;
    return static_cast<Opcode>(opcode.value);
}

std::string_view opcode_name(machine::TargetOpcodeId opcode) noexcept {
    return names[static_cast<std::size_t>(decode_opcode(opcode))];
}

machine::Register gpr(std::uint32_t number, unsigned bits) {
    return machine::Register::physical_register(
        {number}, {static_cast<std::uint16_t>(bits)});
}

machine::Register flags_register() {
    return machine::Register::physical_register({flags}, machine::i64);
}

std::optional<std::uint32_t> physical_register(std::string_view name) {
    if (name == "sp" || name == "wsp") return stack_pointer;
    if (name == "nzcv") return flags;
    if (name.size() < 2) return std::nullopt;
    const auto kind = name.front();
    unsigned number{};
    const auto* end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + 1, end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        (name.size() > 2 && name[1] == '0')) {
        return std::nullopt;
    }
    if ((kind == 'x' || kind == 'w') && number <= 30) return number;
    if (std::string_view("vqdshb").find(kind) != std::string_view::npos && number <= 31) {
        return first_vector + number;
    }
    return std::nullopt;
}

std::string register_name(std::uint32_t number, unsigned bits) {
    const bool wide = bits > 32;
    if (number == stack_pointer) return wide ? "sp" : "wsp";
    if (number == zero_register) return wide ? "xzr" : "wzr";
    return (wide ? "x" : "w") + std::to_string(number);
}

std::string_view condition_name(Condition condition) {
    static constexpr std::array<std::string_view, 14> spellings{
        "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};
    return spellings[static_cast<std::size_t>(condition)];
}

std::string_view branch_mnemonic(Condition condition) {
    static constexpr std::array<std::string_view, 14> mnemonics{
        "b.eq", "b.ne", "b.cs", "b.cc", "b.mi", "b.pl", "b.vs",
        "b.vc", "b.hi", "b.ls", "b.ge", "b.lt", "b.gt", "b.le"};
    return mnemonics[static_cast<std::size_t>(condition)];
}

bool logical_immediate(std::uint64_t value, unsigned bits) {
    if (bits < 64) value &= (std::uint64_t{1} << bits) - 1U;
    return encoders::bitmask(value, bits <= 32 ? 32 : 64).has_value();
}

} // namespace cross::aarch64
