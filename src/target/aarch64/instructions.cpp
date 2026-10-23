// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/instructions.hpp"

#include "target/target.hpp"

#include <charconv>
#include <limits>
#include <tuple>

namespace cross::aarch64 {
namespace {

std::optional<std::int64_t> number(std::string_view text) {
    std::int64_t value{};
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

constexpr std::uint16_t id(OperandTypeId value) { return static_cast<std::uint16_t>(value); }

// The access width of a memory type's suffix: 8 to 128 bits.
std::optional<unsigned> access_bits(std::string_view text) {
    const auto bits = number(text);
    if (!bits || (*bits != 8 && *bits != 16 && *bits != 32 && *bits != 64 && *bits != 128)) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*bits);
}

} // namespace

std::span<const RelocatedSlice> relocated_slices(OperandTypeId type) {
    using Id = OperandTypeId;
    static constexpr RelocatedSlice low26[] = {{0, 26, 0}};
    static constexpr RelocatedSlice imm19[] = {{5, 19, 0}};
    static constexpr RelocatedSlice imm14[] = {{5, 14, 0}};
    static constexpr RelocatedSlice adr[] = {{29, 2, 0}, {5, 19, 2}};
    static constexpr RelocatedSlice imm12[] = {{10, 12, 0}};
    static constexpr RelocatedSlice imm16[] = {{5, 16, 0}};
    switch (type) {
    case Id::Branch26:
    case Id::Call26: return low26;
    case Id::Branch19:
    case Id::Literal19: return imm19;
    case Id::Branch14: return imm14;
    case Id::Adr21:
    case Id::Page21: return adr;
    case Id::AddImmediate:
    case Id::MemoryScaled: return imm12;
    case Id::Move32:
    case Id::Move64: return imm16;
    default: return {};
    }
}

std::optional<OperandType> operand_type(std::string_view name) {
    using Id = OperandTypeId;
    const auto reg = [](Id type, std::string_view register_class, unsigned bits,
                        bool exposable) {
        OperandType result{OperandKind::Register, register_class, 0, 31};
        result.exposable = exposable;
        if (exposable) result.widths = {{bits}};
        if (register_class == "simd") result.required_features = {"fp"};
        result.id = id(type);
        return result;
    };
    // Registers that a built-in can name; sp and wsp are not allocated.
    if (name == "x") return reg(Id::X, "integer", 64, true);
    if (name == "w") return reg(Id::W, "integer", 32, true);
    if (name == "xsp") return reg(Id::XSp, "integer", 64, false);
    if (name == "wsp") return reg(Id::WSp, "integer", 32, false);
    for (const auto& [spelling, type, bits] :
         {std::tuple{std::string_view{"b"}, Id::B, 8U}, std::tuple{std::string_view{"h"}, Id::H, 16U},
          std::tuple{std::string_view{"s"}, Id::S, 32U}, std::tuple{std::string_view{"d"}, Id::D, 64U},
          std::tuple{std::string_view{"q"}, Id::Q, 128U}}) {
        if (name == spelling) return reg(type, "simd", bits, true);
    }
    // Immediates whose field value is not the value itself, which no
    // built-in takes: field values from 0 to `maximum`.
    const auto encoded = [](Id type, std::int64_t maximum) {
        OperandType result{OperandKind::Immediate, {}, 0, maximum};
        result.id = id(type);
        return result;
    };
    if (name == "bitmask32") return encoded(Id::Bitmask32, 0xfff);
    if (name == "bitmask64") return encoded(Id::Bitmask64, 0x1fff);
    if (name == "fp8") return encoded(Id::Fp8, 0xff);
    if (name == "fzero") return encoded(Id::FpZero, 0);
    const auto relocated = [](OperandType result, unsigned bits) {
        result.relocated_bits = bits;
        const auto slices = relocated_slices(static_cast<Id>(result.id));
        result.relocated_slices.assign(slices.begin(), slices.end());
        return result;
    };
    if (name == "uimm12_lsl12") return relocated(encoded(Id::AddImmediate, 0x1fff), 12);
    if (name == "uimm12_lsl12_neg") return encoded(Id::AddImmediateNegated, 0x1fff);
    if (name == "wide32") return encoded(Id::Wide32, 0x1ffff);
    if (name == "wide64") return encoded(Id::Wide64, 0x3ffff);
    if (name == "wide32_inverted") return encoded(Id::WideInverted32, 0x1ffff);
    if (name == "wide64_inverted") return encoded(Id::WideInverted64, 0x3ffff);
    if (name == "movw32") return relocated(encoded(Id::Move32, 0x1ffff), 16);
    if (name == "movw64") return relocated(encoded(Id::Move64, 0x3ffff), 16);
    // Shift and extend modifiers: type:amount and option:amount.
    if (name == "shift32") return encoded(Id::Shift32, 0x9f);
    if (name == "shift64") return encoded(Id::Shift64, 0xbf);
    if (name == "shift32_ror") return encoded(Id::ShiftRotate32, 0xdf);
    if (name == "shift64_ror") return encoded(Id::ShiftRotate64, 0xff);
    if (name == "extend32") return encoded(Id::Extend32, 0x3c);
    if (name == "extend64_w") return encoded(Id::Extend64W, 0x34);
    if (name == "extend64_x") return encoded(Id::Extend64X, 0x3c);
    if (name == "cond") return encoded(Id::Condition, 15);
    if (name == "cond_inverted") return encoded(Id::InvertedCondition, 15);
    if (name == "barrier") return encoded(Id::Barrier, 15);
    if (name == "prfop") return encoded(Id::Prefetch, 31);
    if (name == "pstate") return encoded(Id::PstateField, 0x3f);
    if (name == "sysreg") {
        auto result = encoded(Id::SystemRegister, 0xffff);
        result.minimum = 0x8000;
        return result;
    }
    // PC-relative labels: field values in words (bytes for adr, pages for
    // adrp), and where the word holds them.
    const auto label = [&](Id type, unsigned bits) {
        OperandType result{OperandKind::Label, {}, -(std::int64_t{1} << (bits - 1)),
                           (std::int64_t{1} << (bits - 1)) - 1};
        result.id = id(type);
        return relocated(result, bits);
    };
    if (name == "branch26") return label(Id::Branch26, 26);
    if (name == "call26") return label(Id::Call26, 26);
    if (name == "branch19") return label(Id::Branch19, 19);
    if (name == "branch14") return label(Id::Branch14, 14);
    if (name == "literal19") return label(Id::Literal19, 19);
    if (name == "adr21") return label(Id::Adr21, 21);
    if (name == "adrp21") return label(Id::Page21, 21);
    // Memory: a base register x0-x30 or sp and an offset field.
    const auto memory = [](Id type, std::int64_t minimum, std::int64_t maximum, unsigned bits) {
        OperandType result{OperandKind::Memory, "integer", 0, 31, 0, minimum, maximum, bits};
        result.id = id(type);
        return result;
    };
    if (name == "mem") return memory(Id::Memory, 0, 0, 0);
    if (name == "mem_simm9") return memory(Id::MemoryUnscaled, -256, 255, 0);
    if (name == "mem_pre") return memory(Id::MemoryPre, -256, 255, 0);
    if (name == "mem_post") return memory(Id::MemoryPost, -256, 255, 0);
    if (name.starts_with("mem")) {
        auto rest = name.substr(3);
        const bool indexed = rest.ends_with("_reg");
        if (indexed) rest.remove_suffix(4);
        const auto bits = access_bits(rest);
        if (!bits) return std::nullopt;
        if (indexed) return memory(Id::MemoryRegister, 0, 0x1ff, *bits);
        return relocated(memory(Id::MemoryScaled, 0, 0xfff, *bits), 12);
    }
    if (name.starts_with("pair")) {
        auto rest = name.substr(4);
        auto type = Id::Pair;
        if (rest.ends_with("_pre")) {
            type = Id::PairPre;
            rest.remove_suffix(4);
        } else if (rest.ends_with("_post")) {
            type = Id::PairPost;
            rest.remove_suffix(5);
        }
        const auto bits = access_bits(rest);
        if (!bits || *bits < 32) return std::nullopt;
        return memory(type, -64, 63, *bits);
    }
    // uimmN, simmN, and LOW..HIGH.
    OperandType result{OperandKind::Immediate};
    result.exposable = true;
    result.id = id(Id::Immediate);
    if (const auto dots = name.find(".."); dots != std::string_view::npos) {
        const auto low = number(name.substr(0, dots));
        const auto high = number(name.substr(dots + 2));
        if (!low || !high || *low > *high) return std::nullopt;
        result.minimum = *low;
        result.maximum = *high;
        return result;
    }
    const bool is_signed = name.starts_with("simm");
    if (!is_signed && !name.starts_with("uimm")) return std::nullopt;
    const auto bits = number(name.substr(4));
    if (!bits || *bits < 1 || *bits > 32) return std::nullopt;
    result.minimum = is_signed ? -(std::int64_t{1} << (*bits - 1)) : 0;
    result.maximum = is_signed ? (std::int64_t{1} << (*bits - 1)) - 1
                               : (std::int64_t{1} << *bits) - 1;
    return result;
}

} // namespace cross::aarch64
