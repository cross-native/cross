// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/target.hpp"

#include "common/options.hpp"
#include "target/mips/features.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cross {
namespace {

std::vector<RegisterEntry> target_registers() {
    std::vector<RegisterEntry> result;
    // Instruction operands and naked hard bindings carry sign-canonical
    // words and, from MIPS III, doublewords in any GPR. Managed code binds
    // only allocatable registers; k0/k1 belong to exception handlers.
    const auto gpr = [&](std::string_view name, std::string_view storage,
                         bool compiler_owned = false, bool managed = true) {
        result.push_back({
            name, storage, 64, "integer", "mips1", true,
            compiler_owned || !managed
                ? std::vector<RegisterEntry::ScalarMode>{}
                : std::vector<RegisterEntry::ScalarMode>{
                      {8, false}, {16, false}, {32, false}, {64, false}},
            compiler_owned, {{32, false}, {64, false}}, false,
            storage != "r0"});
    };
    gpr("zero", "r0", true);
    gpr("at", "r1", true);
    gpr("v0", "r2"); gpr("v1", "r3");
    gpr("a0", "r4"); gpr("a1", "r5");
    gpr("a2", "r6"); gpr("a3", "r7");
    gpr("t0", "r8"); gpr("t1", "r9");
    gpr("t2", "r10"); gpr("t3", "r11");
    gpr("t4", "r12"); gpr("t5", "r13");
    gpr("t6", "r14"); gpr("t7", "r15");
    gpr("s0", "r16"); gpr("s1", "r17");
    gpr("s2", "r18"); gpr("s3", "r19");
    gpr("s4", "r20"); gpr("s5", "r21");
    gpr("s6", "r22"); gpr("s7", "r23");
    gpr("t8", "r24"); gpr("t9", "r25");
    gpr("k0", "r26", false, false); gpr("k1", "r27", false, false);
    gpr("gp", "r28", true); gpr("sp", "r29", true);
    gpr("fp", "r30", true); gpr("ra", "r31", true);

    for (unsigned index = 0; index < 32; ++index) {
        static constexpr std::array<std::string_view, 32> names{
            std::string_view{"f0"}, "f1", "f2", "f3", "f4", "f5",
            "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13",
            "f14", "f15", "f16", "f17", "f18", "f19", "f20",
            "f21", "f22", "f23", "f24", "f25", "f26", "f27",
            "f28", "f29", "f30", "f31"};
        static constexpr std::array<std::string_view, 32> storage{
            std::string_view{"f0"}, "f1", "f2", "f3", "f4", "f5",
            "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13",
            "f14", "f15", "f16", "f17", "f18", "f19", "f20",
            "f21", "f22", "f23", "f24", "f25", "f26", "f27",
            "f28", "f29", "f30", "f31"};
        auto modes = std::vector<RegisterEntry::ScalarMode>{{32, true}};
        if ((index & 1U) == 0) modes.push_back({64, true});
        auto instruction_modes = modes;
        result.push_back({names[index], storage[index], 64, "floating",
                          "hard-float", false, std::move(modes), false,
                          std::move(instruction_modes)});
    }
    result.push_back({"hi", "hi", 64, "special", "mips1", false,
                      {}, true});
    result.push_back({"lo", "lo", 64, "special", "mips1", false,
                      {}, true});
    return result;
}

InstructionOperandEntry gpr_operand(InstructionOperandRole role,
                                    unsigned value_bits) {
    InstructionOperandEntry result;
    result.role = role;
    result.allow_register = true;
    result.register_bits = 64;
    result.register_class = "integer";
    result.value_bits = value_bits;
    return result;
}

InstructionOperandEntry fpr_operand(InstructionOperandRole role,
                                    unsigned value_bits) {
    auto result = gpr_operand(role, value_bits);
    result.register_class = "floating";
    return result;
}

InstructionOperandEntry immediate_field(unsigned bits,
                                        bool register_number = false) {
    InstructionOperandEntry result;
    result.allow_immediate = true;
    result.immediate_bits = bits;
    result.assembly_register_number = register_number;
    return result;
}

// An address-only operand: a cache operation names the line holding any
// complete object.
InstructionOperandEntry address_operand() {
    InstructionOperandEntry result;
    result.allow_memory = true;
    return result;
}

// Coprocessor 0 register numbers.
constexpr unsigned cp0_index = 0;
constexpr unsigned cp0_random = 1;
constexpr unsigned cp0_entry_lo0 = 2;
constexpr unsigned cp0_entry_lo1 = 3;
constexpr unsigned cp0_page_mask = 5;
constexpr unsigned cp0_entry_hi = 10;
constexpr unsigned cp0_status = 12;
constexpr unsigned cp0_cause = 13;
constexpr unsigned cp0_epc = 14;
constexpr unsigned cp0_config = 16;
constexpr unsigned cp0_watch_lo = 18;
constexpr unsigned cp0_watch_hi = 19;
constexpr unsigned cp0_tag_lo = 28;
constexpr unsigned cp0_tag_hi = 29;
constexpr unsigned cp0_error_epc = 30;

// Resource names are static strings because registry entries hold views.
constexpr std::string_view cp0_resource(unsigned number) {
    constexpr std::array<std::string_view, 32> names{
        "cp0.0", "cp0.1", "cp0.2", "cp0.3", "cp0.4", "cp0.5", "cp0.6",
        "cp0.7", "cp0.8", "cp0.9", "cp0.10", "cp0.11", "cp0.12", "cp0.13",
        "cp0.14", "cp0.15", "cp0.16", "cp0.17", "cp0.18", "cp0.19",
        "cp0.20", "cp0.21", "cp0.22", "cp0.23", "cp0.24", "cp0.25",
        "cp0.26", "cp0.27", "cp0.28", "cp0.29", "cp0.30", "cp0.31"};
    return names[number];
}

HazardFact cp0_fact(unsigned number, unsigned stage) {
    return {cp0_resource(number), stage};
}

// Pipeline stages follow the R4000 coprocessor 0 hazard table, which the
// VR4300 keeps: CP0 data written by MTC0 is available at stage 7, TLB
// writes at stage 8, and MFC0 reads at stage 4.
std::vector<InstructionEntry> target_instructions() {
    using Role = InstructionOperandRole;
    using Control = InstructionControlEffect;
    std::vector<InstructionEntry> result;
    const auto add = [&](std::string_view name, std::string_view feature,
                         std::string_view mnemonic,
                         std::vector<InstructionOperandEntry> operands,
                         std::vector<std::string_view> reads,
                         std::vector<std::string_view> writes,
                         Control control = Control::None)
        -> InstructionEntry& {
        result.emplace_back(name, feature, mnemonic, std::move(operands),
                            std::move(reads), std::move(writes), 0, control);
        return result.back();
    };
    const auto system = [](InstructionEntry& entry) -> InstructionEntry& {
        entry.privileged = true;
        entry.volatile_effect = true;
        return entry;
    };

    add("$::_nop", "mips", "nop", {}, {}, {});

    // Coprocessor 0 moves. Reads are volatile: Count, Random, Cause, EPC,
    // and BadVAddr change without a program write.
    for (const auto& [move_from, move_to, feature, bits] :
         {std::tuple{std::string_view{"mfc0"}, std::string_view{"mtc0"},
                     std::string_view{"mips"}, 32U},
          std::tuple{std::string_view{"dmfc0"}, std::string_view{"dmtc0"},
                     std::string_view{"mips3"}, 64U}}) {
        auto& read = system(add(
            move_from == "mfc0" ? "$::_mfc0" : "$::_dmfc0", feature,
            move_from,
            {gpr_operand(Role::Output, bits), immediate_field(5, true)},
            {"cp0"}, {}));
        read.hazard_uses = {{"cp0", 4, 1}};
        auto& write = system(add(
            move_to == "mtc0" ? "$::_mtc0" : "$::_dmtc0", feature, move_to,
            {gpr_operand(Role::Input, bits), immediate_field(5, true)},
            {}, {"cp0"}));
        write.hazard_writes = {{"cp0", 7, 1}};
    }

    // TLB maintenance.
    auto& probe = system(add("$::_tlbp", "mips", "tlbp", {},
                             {"cp0", "tlb"}, {"cp0"}));
    probe.hazard_uses = {cp0_fact(cp0_page_mask, 3),
                         cp0_fact(cp0_entry_hi, 3), {"tlb", 4}};
    probe.hazard_writes = {cp0_fact(cp0_index, 7)};
    auto& read = system(add("$::_tlbr", "mips", "tlbr", {},
                            {"cp0", "tlb"}, {"cp0"}));
    read.hazard_uses = {cp0_fact(cp0_index, 5), {"tlb", 5}};
    read.hazard_writes = {cp0_fact(cp0_page_mask, 8),
                          cp0_fact(cp0_entry_hi, 8),
                          cp0_fact(cp0_entry_lo0, 8),
                          cp0_fact(cp0_entry_lo1, 8)};
    for (const auto& [name, mnemonic, selector] :
         {std::tuple{std::string_view{"$::_tlbwi"}, std::string_view{"tlbwi"},
                     cp0_index},
          std::tuple{std::string_view{"$::_tlbwr"}, std::string_view{"tlbwr"},
                     cp0_random}}) {
        auto& write = system(add(name, "mips", mnemonic, {}, {"cp0"},
                                 {"tlb"}));
        write.hazard_uses = {cp0_fact(selector, 5),
                             cp0_fact(cp0_page_mask, 5),
                             cp0_fact(cp0_entry_hi, 5),
                             cp0_fact(cp0_entry_lo0, 5),
                             cp0_fact(cp0_entry_lo1, 5)};
        write.hazard_writes = {{"tlb", 8}};
    }

    // Exception return, cache maintenance, and memory ordering. A cache
    // operation translates its address like a load; Index Load Tag
    // (operations 4-7) writes TagLo/TagHi. A store to the same line needs
    // two non-load, non-cache instructions before the cache operation.
    const auto index_load_tag = [](unsigned number) {
        return HazardFact{cp0_resource(number), 8, std::nullopt, 0,
                          {4, 5, 6, 7}};
    };
    for (const auto feature : {std::string_view{"mips3"},
                               std::string_view{"mips32"}}) {
        auto& exception_return = system(add(
            "$::_eret", feature, "eret", {}, {"cp0", "tlb"}, {"cp0"},
            Control::RawReturn));
        exception_return.hazard_uses = {cp0_fact(cp0_epc, 4),
                                        cp0_fact(cp0_error_epc, 4),
                                        {"tlb", 4},
                                        cp0_fact(cp0_status, 3)};
        auto& cache = system(add(
            "$::_cache", feature, "cache",
            {immediate_field(5), address_operand()},
            {"memory", "cp0", "tlb"}, {"memory", "cp0"}));
        cache.hazard_uses = {{"tlb", 4}, {"dcache.line", 3}};
        cache.hazard_writes = {index_load_tag(cp0_tag_lo),
                               index_load_tag(cp0_tag_hi)};
    }
    auto& barrier = add("$::_sync", "mips2", "sync", {}, {"memory"},
                        {"memory"});
    barrier.volatile_effect = true;

    // Floating-point control and status.
    auto& control_read = add(
        "$::_cfc1", "hard-float", "cfc1",
        {gpr_operand(Role::Output, 32), immediate_field(5, true)},
        {"fcsr"}, {});
    control_read.volatile_effect = true;
    auto& control_write = add(
        "$::_ctc1", "hard-float", "ctc1",
        {gpr_operand(Role::Input, 32), immediate_field(5, true)},
        {}, {"fcsr"});
    control_write.volatile_effect = true;

    // Pure floating forms; `.d` needs a double-precision FPU.
    for (const auto& [name, mnemonic, feature] :
         {std::tuple{std::string_view{"$::_sqrt"}, std::string_view{"sqrt"},
                     std::string_view{"mips2"}},
          std::tuple{std::string_view{"$::_abs"}, std::string_view{"abs"},
                     std::string_view{"hard-float"}},
          std::tuple{std::string_view{"$::_neg"}, std::string_view{"neg"},
                     std::string_view{"hard-float"}}}) {
        for (const auto bits : {32U, 64U}) {
            auto& entry = add(
                name, feature,
                mnemonic == "sqrt" ? (bits == 32 ? "sqrt.s" : "sqrt.d")
                : mnemonic == "abs" ? (bits == 32 ? "abs.s" : "abs.d")
                                    : (bits == 32 ? "neg.s" : "neg.d"),
                {fpr_operand(Role::Output, bits),
                 fpr_operand(Role::Input, bits)},
                {}, {});
            if (feature != "hard-float") {
                entry.required_features = {"hard-float"};
            }
            if (bits == 64) entry.forbidden_features = {"single-float"};
        }
    }

    // Control transfers that leave a naked function.
    for (const auto& [feature, bits] :
         {std::pair{std::string_view{"mips"}, 32U},
          std::pair{std::string_view{"mips3"}, 64U}}) {
        add("$::_jr", feature, "jr", {gpr_operand(Role::Input, bits)}, {},
            {}, Control::RawReturn);
    }
    return result;
}

std::vector<HazardEventEntry> target_hazard_events() {
    std::vector<HazardEventEntry> result;
    // Loads and stores translate through the TLB with the current ASID,
    // mode, and kseg0 cacheability, and watch for WatchLo/WatchHi.
    for (const auto event : {HazardEvent::Load, HazardEvent::Store}) {
        result.push_back({event, {"tlb", 4}});
        for (const auto number : {cp0_entry_hi, cp0_status, cp0_config,
                                  cp0_watch_lo, cp0_watch_hi}) {
            result.push_back({event, cp0_fact(number, 4)});
        }
    }
    // Interrupts are sampled against Status and Cause for every
    // instruction, and coprocessor usability is tested at stage 2.
    result.push_back({HazardEvent::Instruction, cp0_fact(cp0_status, 3)});
    result.push_back({HazardEvent::Instruction, cp0_fact(cp0_cause, 3)});
    result.push_back({HazardEvent::Coprocessor, cp0_fact(cp0_status, 2)});
    result.push_back({HazardEvent::Store, {"dcache.line", 6}, true,
                      {HazardEvent::Load, HazardEvent::Cache}});
    return result;
}

unsigned word_constant_materialization_cost(std::uint32_t value) {
    const auto signed_value = static_cast<std::int32_t>(value);
    if ((signed_value >= std::numeric_limits<std::int16_t>::min() &&
         signed_value <= std::numeric_limits<std::int16_t>::max()) ||
        value <= std::numeric_limits<std::uint16_t>::max() ||
        (value & std::numeric_limits<std::uint16_t>::max()) == 0) {
        return 1;
    }
    return 2;
}

unsigned mips64_constant_materialization_cost(std::uint64_t value) {
    if (value == 0) return 1;

    const auto word = static_cast<std::uint32_t>(value);
    const auto sign_extended_word = (word & 0x80000000U) != 0
        ? 0xffffffff00000000ULL | word
        : static_cast<std::uint64_t>(word);
    if (value == sign_extended_word) {
        return word_constant_materialization_cost(word);
    }

    // GNU as first searches for an unsigned 16-bit value plus one DSLL.
    // This catches sparse constants such as 1<<32 without committing to the
    // fixed four-chunk construction used for a general `dli`.
    const auto low_bit = static_cast<unsigned>(std::countr_zero(value));
    const auto shifted = value >> low_bit;
    if (std::bit_width(shifted) <= 16) return 2;

    // A shifted run of ones is cheaper to create from ADDIU -1 followed by
    // one or two shifts.  This is the other GNU-as fast path used by the
    // compiler's emitted `dli` pseudo-op.
    if ((shifted & (shifted + 1U)) == 0) {
        const auto leading_zeroes = static_cast<unsigned>(
            std::countl_zero(static_cast<std::uint32_t>(value >> 32U)));
        if (leading_zeroes != 0) return low_bit == 0 ? 2U : 3U;
    }

    // General GNU-as construction: load a sign-extended high word, then
    // append the nonzero 16-bit pieces of the low word with DSLL/ORI.
    const auto high = static_cast<std::uint32_t>(value >> 32U);
    const auto low = static_cast<std::uint32_t>(value);
    unsigned cost = high == 0 ? 0U
                              : word_constant_materialization_cost(high);
    const bool loaded = high != 0;
    if ((low & 0xffff0000U) == 0) {
        if (loaded) ++cost; // DSLL32
    } else {
        if (loaded) ++cost; // DSLL 16
        ++cost;             // ORI high half
        ++cost;             // DSLL 16
    }
    if ((low & 0xffffU) != 0) ++cost;
    return cost;
}

// DIV/DIVU and MULT/MULTU at the word width, their D forms from MIPS III.
// Before MIPS III a doubleword quotient is a pair-legalized software loop.
std::optional<unsigned> integer_division_cost(
    const Subtarget& subtarget, const IntegerOperationCostQuery& query) {
    if (query.bits == 32) return 35U;
    if (query.bits == 64 && subtarget.has_feature(mips::Feature::Mips3)) {
        return 69U;
    }
    return std::nullopt;
}

std::optional<unsigned> integer_multiply_high_cost(
    const Subtarget& subtarget, const IntegerOperationCostQuery& query) {
    const bool mips3 = subtarget.has_feature(mips::Feature::Mips3);
    if (query.bits == 32) return mips3 ? 6U : 12U;
    if (query.bits == 64 && mips3) return 9U;
    return std::nullopt;
}

unsigned integer_constant_materialization_cost(
    const Subtarget& subtarget, const IntegerConstantCostQuery& query) {
    if (query.bits == 0) return 1;
    if (query.bits > 64 || query.high != 0) return 4;

    const auto width = std::min(query.bits, 64U);
    const auto mask = width == 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << width) - 1U;
    const auto value = query.low & mask;
    if (width <= 32) {
        return word_constant_materialization_cost(
            static_cast<std::uint32_t>(value));
    }

    if (!subtarget.has_feature(mips::Feature::Mips3)) {
        return word_constant_materialization_cost(
                   static_cast<std::uint32_t>(value)) +
               word_constant_materialization_cost(
                   static_cast<std::uint32_t>(value >> 32U));
    }

    return mips64_constant_materialization_cost(value);
}

const SubtargetTable subtargets{
    "mips",
    {
        {"mips1", true, false, {}, {}},
        {"mips2", false, true, {"mips1"}, {}},
        {"mips3", false, true, {"mips2"}, {}},
        {"mips4", false, true, {"mips3"}, {}},
        {"mips5", false, true, {"mips4"}, {}},
        {"mips32", false, true, {"mips2"}, {"mips3", "mips4", "mips5"}},
        {"mips32r2", false, true, {"mips32"}, {}},
        {"mips64", false, true, {"mips3"}, {"mips32", "mips32r2"}},
        {"hard-float", false, true, {"mips1"}, {"soft-float"}},
        {"soft-float", false, true, {"mips1"}, {"hard-float"}},
        {"fp32", false, true, {"hard-float"}, {"fp64", "fpxx"}},
        {"fp64", false, true, {"hard-float", "mips3"}, {"fp32", "fpxx"}},
        {"fpxx", false, true, {"hard-float", "mips2"}, {"fp32", "fp64"}},
        {"odd-spreg", false, true, {"hard-float"}, {}},
        {"llsc", false, true, {"mips2"}, {}},
        {"branch-likely", false, true, {"mips2"}, {}},
        {"fix4300", false, true, {"mips3", "hard-float"}, {}},
        {"abicalls", false, true, {"mips1"}, {}},
        {"mips16", false, true, {"mips1"}, {"micromips"}},
        {"micromips", false, true, {"mips32"}, {"mips16"}},
        {"single-float", false, true, {"hard-float"}, {"fp64", "fpxx"}},
        {"allegrex", false, false,
         {"mips2", "hard-float", "single-float"}, {}},
        {"cond-move", false, true, {"mips1"}, {}},
        {"rotate", false, true, {"mips1"}, {}},
        {"vfpu", false, true, {"allegrex"}, {}},
        // Pipeline interlocks are CPU facts rather than user-visible ISA
        // extensions.  The printer uses their absence to insert the exact
        // architectural separation required by older implementations.
        {"load-interlocks", false, false, {"mips1"}, {}},
        {"fpu-transfer-interlocks", false, false, {"mips1"}, {}},
        {"fpu-compare-interlocks", false, false, {"mips1"}, {}},
        {"hilo-interlocks", false, false, {"mips1"}, {}},
    },
    {
        {"generic", {"mips1", "hard-float", "fp32", "odd-spreg"}},
        {"mips1", {"mips1", "hard-float", "fp32", "odd-spreg"}},
        {"r2000", {"mips1", "hard-float", "fp32", "odd-spreg"}},
        {"r3000", {"mips1", "hard-float", "fp32", "odd-spreg"}},
        {"mips2", {"mips2", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"r6000", {"mips2", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"allegrex", {"mips2", "hard-float", "fp32", "single-float",
                       "odd-spreg", "llsc", "branch-likely", "allegrex",
                       "cond-move", "rotate", "load-interlocks",
                       "fpu-transfer-interlocks", "hilo-interlocks"}},
        {"mips3", {"mips3", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"r4000", {"mips3", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"r4400", {"mips3", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"r4600", {"mips3", "hard-float", "fp32", "odd-spreg",
                    "llsc", "branch-likely", "load-interlocks"}},
        {"vr4300", {"mips3", "hard-float", "fp32", "odd-spreg",
                     "llsc", "branch-likely", "load-interlocks"}},
        {"mips4", {"mips4", "hard-float", "fp64", "odd-spreg",
                    "llsc", "branch-likely", "cond-move",
                    "load-interlocks", "fpu-transfer-interlocks",
                    "fpu-compare-interlocks"}},
        {"mips5", {"mips5", "hard-float", "fp64", "odd-spreg",
                    "llsc", "branch-likely", "cond-move",
                    "load-interlocks", "fpu-transfer-interlocks",
                    "fpu-compare-interlocks"}},
        {"mips32", {"mips32", "hard-float", "fp32", "odd-spreg",
                     "llsc", "branch-likely", "cond-move",
                     "load-interlocks", "fpu-transfer-interlocks",
                     "fpu-compare-interlocks", "hilo-interlocks"}},
        {"mips32r2", {"mips32r2", "hard-float", "fp32", "odd-spreg",
                       "llsc", "branch-likely", "cond-move", "rotate",
                       "load-interlocks", "fpu-transfer-interlocks",
                       "fpu-compare-interlocks", "hilo-interlocks"}},
        {"mips64", {"mips64", "hard-float", "fp64", "odd-spreg",
                     "llsc", "branch-likely", "cond-move",
                     "load-interlocks", "fpu-transfer-interlocks",
                     "fpu-compare-interlocks", "hilo-interlocks"}},
    },
};

std::vector<OptionDefinition> target_options() {
    std::vector<OptionDefinition> result{
        {"m.arch", {}, OptionValueKind::Text, std::string("generic"), {},
         0, 0, OptionCategory::Target, false,
         OptionImplementation::Implemented,
         "instruction-compatible MIPS CPU or ISA"},
        {"m.cmodel", {}, OptionValueKind::Enumeration,
         std::string("small"), {"small", "large"},
         0, 0, OptionCategory::Target, false,
         OptionImplementation::Implemented,
         "MIPS symbol addressability model: small keeps sign-extended "
         "32-bit symbol addresses, large materializes complete 64-bit "
         "addresses and calls through a register"},
        {"m.tune", {}, OptionValueKind::Text, std::string("generic"), {},
         0, 0, OptionCategory::Target, true,
         OptionImplementation::Implemented,
         "MIPS scheduling and cost CPU"},
        {"m.risc-cisc-balance", {}, OptionValueKind::Unsigned,
         std::uint64_t{50}, {}, 0, 100, OptionCategory::Target, true,
         OptionImplementation::Implemented,
         "continuous MIR cost position: 0 is RISC-like, 100 is CISC-like"},
    };
    const auto boolean = [&](std::string name, bool value,
                             std::string description,
                             OptionImplementation implementation =
                                 OptionImplementation::Implemented) {
        result.push_back({"m." + std::move(name), {},
                          OptionValueKind::Boolean, value, {}, 0, 0,
                          OptionCategory::Target, false, implementation,
                          std::move(description)});
    };
    boolean("mips2", false, "enable MIPS II instructions");
    boolean("mips3", false, "enable MIPS III instructions and 64-bit GPR operations");
    boolean("mips4", false, "enable MIPS IV instructions");
    boolean("mips5", false, "enable MIPS V instructions");
    boolean("mips32", false, "enable the MIPS32 ISA");
    boolean("mips32r2", false, "enable the MIPS32 Release 2 ISA");
    boolean("mips64", false, "enable the MIPS64 ISA");
    boolean("hard-float", true, "use hardware floating-point ABI endpoints");
    boolean("soft-float", false, "use integer floating-point ABI endpoints");
    boolean("fp32", true, "use the paired 32-bit FPR convention");
    boolean("fp64", false, "use 64-bit floating-point registers");
    boolean("fpxx", false, "use the o32 FPXX interoperable convention");
    boolean("odd-spreg", true, "allow odd single-precision FPRs");
    boolean("llsc", false, "enable load-linked/store-conditional atomics");
    boolean("branch-likely", false, "enable branch-likely instructions");
    boolean("fix4300", false, "work around the early VR4300 FP multiply erratum");
    boolean("long-calls", false,
            "call through a register so the callee may lie outside the "
            "caller's 256 MB region");
    boolean("abicalls", false, "enable SVR4 PIC ABI calls",
            OptionImplementation::Partial);
    boolean("mips16", false, "enable MIPS16 encoding",
            OptionImplementation::Partial);
    boolean("micromips", false, "enable microMIPS encoding",
            OptionImplementation::Partial);
    boolean("single-float", false,
            "limit hardware floating point to single precision");
    boolean("cond-move", false,
            "enable integer conditional-move selection");
    boolean("rotate", false, "enable native variable rotates");
    boolean("vfpu", false, "enable the Allegrex VFPU extension",
            OptionImplementation::Partial);
    return result;
}

TargetInfo make_target(ByteOrder order,
                       std::vector<std::string_view> prefixes) {
    return {
        "mips", std::move(prefixes),
        {order, 8, 16, 8,
         order == ByteOrder::Big
             ? BitFieldOrder::MostSignificantFirst
             : BitFieldOrder::LeastSignificantFirst,
         BitFieldUnitSharing::SameUnqualifiedBase,
         BitFieldPlacement::AlignedUnits, CodeAddressRepresentation::Flat},
        target_registers(),
        {
            {"i8", 8, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"u8", 8, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"i16", 16, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"u16", 16, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"i32", 32, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"u32", 32, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            // MIPS I/II use target-legalized 32-bit GPR pairs; MIPS III and
            // later select native 64-bit GPR operations for the same types.
            {"i64", 64, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"u64", 64, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"iptr", 32, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
            {"uptr", 32, "mips1", PatchAddressRepresentation::FlatUptr, false, true},
        },
        target_instructions(),
        {{32, "llsc"}},
        {},
        target_options(),
        &subtargets,
        {integer_constant_materialization_cost, integer_division_cost,
         integer_multiply_high_cost},
        {{0, 0, 0, 0, true, true, true, true, false, true, true}},
        // Instruction memory operands are a GPR base plus a signed 16-bit
        // displacement; MIPS has no scaled index.
        {{32, 64, "integer", {0}, {}, {}, 16, "mips"},
         {64, 64, "integer", {0}, {}, {}, 16, "mips3"}},
        {{"atomics", "m.llsc"}},
        {{"eabi32", 32}},
        target_hazard_events(),
        NakedLowering::Constrained,
        {{"$::sqrt", "$::_sqrt"}},
    };
}

const TargetInfo big_endian = make_target(
    ByteOrder::Big, {"mipsallegrex", "mips64", "mips"});
const TargetInfo little_endian = make_target(
    ByteOrder::Little, {"mipsallegrexel", "mips64el", "mipsel"});

} // namespace

const TargetInfo& mips_target() { return big_endian; }
const TargetInfo& mipsel_target() { return little_endian; }

} // namespace cross
