// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// AArch64 Machine IR vocabulary: opcodes, physical register numbers, and
// condition codes, shared by selection, the spill stage, frame lowering,
// and the assembly printer.

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cross::aarch64 {

enum class Opcode : std::uint16_t {
    None,
#define X(name, spelling) name,
#include "target/aarch64/machine_opcodes.def"
#undef X
    Count,
};

[[nodiscard]] constexpr machine::TargetOpcodeId machine_opcode(Opcode opcode) {
    return {static_cast<std::uint32_t>(opcode)};
}
[[nodiscard]] Opcode decode_opcode(machine::TargetOpcodeId opcode) noexcept;
[[nodiscard]] std::string_view opcode_name(machine::TargetOpcodeId opcode) noexcept;

// Physical storage: x0-x30 are 0-30, sp is 31, v0-v31 are 32-63, NZCV is
// 64, and the zero register, which reads as zero and discards writes, 65.
inline constexpr std::uint32_t frame_pointer = 29;
inline constexpr std::uint32_t link_register = 30;
inline constexpr std::uint32_t stack_pointer = 31;
inline constexpr std::uint32_t first_vector = 32;
inline constexpr std::uint32_t flags = 64;
inline constexpr std::uint32_t zero_register = 65;

// A general register, sp, or the zero register as a value of `bits`.
[[nodiscard]] machine::Register gpr(std::uint32_t number, unsigned bits);
[[nodiscard]] machine::Register flags_register();
// The storage a model register name gives: x0-x30 and their w views, sp,
// the views of v0-v31, and nzcv; none for other names.
[[nodiscard]] std::optional<std::uint32_t> physical_register(std::string_view name);
// The assembler spelling of a general register or sp as a value of `bits`.
[[nodiscard]] std::string register_name(std::uint32_t number, unsigned bits);
// The DWARF number of a general register or sp.
[[nodiscard]] constexpr unsigned dwarf_register(std::uint32_t number) {
    return number < first_vector ? number : 64U + (number - first_vector);
}

// Condition codes in their encoding order.
enum class Condition : std::uint8_t { Eq, Ne, Hs, Lo, Mi, Pl, Vs, Vc, Hi, Ls, Ge, Lt, Gt, Le };
[[nodiscard]] constexpr Condition invert(Condition condition) {
    return static_cast<Condition>(static_cast<unsigned>(condition) ^ 1U);
}
[[nodiscard]] std::string_view condition_name(Condition condition);
// The b.cond mnemonic of the selection contract: b.cs and b.cc for hs and
// lo, which are their aliases.
[[nodiscard]] std::string_view branch_mnemonic(Condition condition);

// Whether `value` of `bits` (32 or 64) is a logical immediate.
[[nodiscard]] bool logical_immediate(std::uint64_t value, unsigned bits);
// Whether `value` fits an add or subtract immediate: 12 bits, optionally
// shifted left by 12.
[[nodiscard]] constexpr bool arithmetic_immediate(std::uint64_t value) {
    return value <= 0xfffU || ((value & 0xfffU) == 0 && value <= 0xfff000U);
}

} // namespace cross::aarch64
