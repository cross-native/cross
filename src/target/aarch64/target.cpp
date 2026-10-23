// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/target.hpp"

#include "common/options.hpp"
#include "target/aarch64/assembler.hpp"
#include "target/aarch64/instructions.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace cross {
namespace {

constexpr std::array<std::string_view, 31> x_names{
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20",
    "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30"};
constexpr std::array<std::string_view, 31> w_names{
    "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10",
    "w11", "w12", "w13", "w14", "w15", "w16", "w17", "w18", "w19", "w20",
    "w21", "w22", "w23", "w24", "w25", "w26", "w27", "w28", "w29", "w30"};

// The views of one SIMD and floating-point register, widest first.
template <char View>
constexpr std::array<std::string_view, 32> simd_names() {
    if constexpr (View == 'v') {
        return {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
                "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18",
                "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27",
                "v28", "v29", "v30", "v31"};
    } else if constexpr (View == 'q') {
        return {"q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9",
                "q10", "q11", "q12", "q13", "q14", "q15", "q16", "q17", "q18",
                "q19", "q20", "q21", "q22", "q23", "q24", "q25", "q26", "q27",
                "q28", "q29", "q30", "q31"};
    } else if constexpr (View == 'd') {
        return {"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
                "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18",
                "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27",
                "d28", "d29", "d30", "d31"};
    } else if constexpr (View == 's') {
        return {"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9",
                "s10", "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18",
                "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "s27",
                "s28", "s29", "s30", "s31"};
    } else if constexpr (View == 'h') {
        return {"h0", "h1", "h2", "h3", "h4", "h5", "h6", "h7", "h8", "h9",
                "h10", "h11", "h12", "h13", "h14", "h15", "h16", "h17", "h18",
                "h19", "h20", "h21", "h22", "h23", "h24", "h25", "h26", "h27",
                "h28", "h29", "h30", "h31"};
    } else {
        return {"b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9",
                "b10", "b11", "b12", "b13", "b14", "b15", "b16", "b17", "b18",
                "b19", "b20", "b21", "b22", "b23", "b24", "b25", "b26", "b27",
                "b28", "b29", "b30", "b31"};
    }
}

std::vector<RegisterEntry> target_registers() {
    using Mode = RegisterEntry::ScalarMode;
    std::vector<RegisterEntry> result;
    // x29 holds the frame record and x30 the return address, which no
    // managed binding takes; x18's role is the platform's.
    for (std::size_t index = 0; index < x_names.size(); ++index) {
        const bool managed = index < 29;
        result.push_back({x_names[index], x_names[index], 64, "integer",
                          "aarch64", true,
                          managed ? std::vector<Mode>{{64, false}}
                                  : std::vector<Mode>{},
                          index == 29, {{64, false}}});
        result.push_back({w_names[index], x_names[index], 32, "integer",
                          "aarch64", false,
                          managed ? std::vector<Mode>{{8, false}, {16, false},
                                                      {32, false}}
                                  : std::vector<Mode>{},
                          index == 29, {{32, false}}});
    }
    result.push_back({"sp", "sp", 64, "integer", "aarch64", true, {}, true,
                      {{64, false}}});
    result.back().instruction_writable = false;
    result.push_back({"wsp", "sp", 32, "integer", "aarch64", false, {}, true,
                      {{32, false}}});
    result.back().instruction_writable = false;
    // The zero register reads as zero and discards writes.
    result.push_back({"xzr", "xzr", 64, "integer", "aarch64", false, {},
                      false, {{64, false}}});
    result.back().instruction_writable = false;
    result.push_back({"wzr", "xzr", 32, "integer", "aarch64", false, {},
                      false, {{32, false}}});
    result.back().instruction_writable = false;

    const auto add_views = [&](const std::array<std::string_view, 32>& names,
                               unsigned bits, std::vector<Mode> modes,
                               bool vectors) {
        static constexpr auto storage = simd_names<'v'>();
        for (std::size_t index = 0; index < names.size(); ++index) {
            result.push_back({names[index], storage[index], bits, "simd", "fp",
                              false, modes, false, modes});
            result.back().instruction_vector_values = vectors;
        }
    };
    add_views(simd_names<'v'>(), 128, {}, true);
    add_views(simd_names<'q'>(), 128, {}, true);
    add_views(simd_names<'d'>(), 64, {{64, true}}, true);
    add_views(simd_names<'s'>(), 32, {{32, true}}, false);
    add_views(simd_names<'h'>(), 16, {}, false);
    add_views(simd_names<'b'>(), 8, {}, false);

    // The condition flags and the floating-point control and status
    // registers.
    result.push_back({"nzcv", "nzcv", 64, "special", "aarch64", false, {}});
    result.push_back({"fpcr", "fpcr", 64, "special", "fp", false, {}});
    result.push_back({"fpsr", "fpsr", 64, "special", "fp", false, {}});
    return result;
}

#define LIST(...) {__VA_ARGS__}
const SubtargetTable subtargets{
    "aarch64",
    {
#define X(name, spelling, description, prerequisites, implies, on, off) \
    {spelling, false, true, LIST prerequisites, {}, {}, LIST implies,     \
     LIST on, LIST off},
#include "target/aarch64/features.def"
#undef X
    },
};
#undef LIST

std::vector<OptionDefinition> target_options() {
    // Code generation for AArch64 does not exist, so every option is
    // partial: it selects model entries and the features that the
    // assembler accepts instructions of.
    std::vector<OptionDefinition> result{
        {"m.arch", {}, OptionValueKind::Text, std::string("generic"), {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Partial,
         "AArch64 CPU or architecture level, with +FEATURE and +noFEATURE "
         "modifiers"},
        {"m.tune", {}, OptionValueKind::Text, std::string("generic"), {}, 0, 0,
         OptionCategory::Target, true, OptionImplementation::Partial,
         "AArch64 scheduling and cost CPU"},
        {"m.risc-cisc-balance", {}, OptionValueKind::Unsigned,
         std::uint64_t{50}, {}, 0, 100, OptionCategory::Target, true,
         OptionImplementation::Partial,
         "continuous MIR cost position: 0 is RISC-like, 100 is CISC-like"},
    };
#define X(name, spelling, description, ...)                                  \
    result.push_back({"m." + std::string(spelling), {},                      \
                      OptionValueKind::Boolean, false, {}, 0, 0,             \
                      OptionCategory::Target, false,                         \
                      OptionImplementation::Partial, description});
#include "target/aarch64/features.def"
#undef X
    return result;
}

// The number that encodes a general or SIMD and floating-point register:
// 31 for sp, wsp, xzr, and wzr.
std::optional<unsigned> register_number(std::string_view name) {
    if (name == "sp" || name == "wsp" || name == "xzr" || name == "wzr") return 31;
    if (name.size() < 2 || std::string_view("xwvqdshb").find(name[0]) == std::string_view::npos) {
        return std::nullopt;
    }
    unsigned value{};
    const auto* end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + 1, end, value);
    const unsigned limit = name[0] == 'x' || name[0] == 'w' ? 30 : 31;
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > limit ||
        (name.size() > 2 && name[1] == '0')) {
        return std::nullopt;
    }
    return value;
}

const InstructionModel instruction_model{
    32, aarch64::operand_type, register_number, {}, {}, {}, {},
};

TargetInfo make_target() {
    TargetInfo result;
    result.architecture = "aarch64";
    result.triple_prefixes = {{"aarch64-", 64}};
    // AAPCS64: 16-byte alignment for 128-bit scalars, bit-fields from the
    // least significant bit in units of their declared type. With
    // FPCR.DN clear, an operation propagates its first signaling NaN
    // operand, otherwise its first NaN operand; the default NaN is
    // positive.
    result.data_layout = {
        ByteOrder::Little, 16, 16, 16, BitFieldOrder::LeastSignificantFirst,
        BitFieldUnitSharing::SameStorageSize, BitFieldPlacement::NextAvailableBit,
        CodeAddressRepresentation::Flat,
        floating::NanEncoding{
            .propagation = {floating::NanPropagation::FirstSignaling,
                            floating::NanPropagation::FirstSignaling,
                            floating::NanPropagation::FirstSignaling,
                            floating::NanPropagation::FirstSignaling}}};
    result.registers = target_registers();
    result.options = target_options();
    result.subtargets = &subtargets;
    result.address_spaces = {{0, 0, 0, 0, true, true, true, true, false, true, true}};
    // The models give the instruction table, which Cross's assembler
    // encodes.
    result.instruction_model = &instruction_model;
    result.integrated_assembler = &aarch64::assemble;
    return result;
}

// Not const: loading the models installs their instruction table.
TargetInfo target = make_target();

} // namespace

const TargetInfo& aarch64_target() { return target; }

} // namespace cross
