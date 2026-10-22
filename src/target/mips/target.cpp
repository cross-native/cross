// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/target.hpp"

#include "common/options.hpp"
#include "target/mips/assembler.hpp"
#include "target/mips/bit_operations.hpp"
#include "target/mips/elf.hpp"
#include "target/mips/features.hpp"
#include "target/mips/instructions.hpp"
#include "target/mips/linker.hpp"
#include "target/mips/select_operations.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
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
    // only allocatable registers; k0/k1 belong to exception handlers. gp,
    // sp, and fp hold compiler state across calls, so no clobber names them.
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
    gpr("zero", "r0", false, false);
    gpr("at", "r1", false, false);
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
    gpr("fp", "r30", true); gpr("ra", "r31", false, false);

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
    result.push_back({"hi", "hi", 64, "special", "mips1", false, {}});
    result.push_back({"lo", "lo", 64, "special", "mips1", false, {}});
    return result;
}

// Coprocessor 0 register numbers.
constexpr unsigned cp0_entry_hi = 10;
constexpr unsigned cp0_status = 12;
constexpr unsigned cp0_cause = 13;
constexpr unsigned cp0_config = 16;
constexpr unsigned cp0_watch_lo = 18;
constexpr unsigned cp0_watch_hi = 19;

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
// VR4300 keeps; the instruction table states those of the coprocessor 0
// instructions.
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

// The multiply and divide unit: MULT/MULTU and DIV/DIVU, their D forms from
// MIPS III, and the MFLO or MFHI that reads the result, or the MIPS32 and
// MIPS64 MUL where it pays; Release 6's three-operand forms. Before MIPS III
// a doubleword operation is a pair-legalized sequence.
std::vector<InstructionCostId> integer_operation_instructions(
    const Subtarget& subtarget, CostedIntegerOperation operation,
    const IntegerOperationCostQuery& query) {
    using mips::Instruction;
    const bool wide = query.bits == 64;
    // Release 6 computes each result in one instruction.
    const bool r6 = mips::release6(subtarget);
    // Before MIPS III a 64-bit multiply-high combines the four products of
    // the words; the other doubleword operations are software loops.
    if (wide && !subtarget.has_feature(mips::Feature::Mips3)) {
        if (operation != CostedIntegerOperation::MultiplyHigh) return {};
        if (r6) {
            return {Instruction::Muhu, Instruction::Mulu, Instruction::Muhu,
                    Instruction::Mulu, Instruction::Muhu, Instruction::Mulu,
                    Instruction::Muhu};
        }
        return {Instruction::Multu, Instruction::Mfhi, Instruction::Multu,
                Instruction::Mflo, Instruction::Multu, Instruction::Mflo,
                Instruction::Multu, Instruction::Mflo};
    }
    switch (operation) {
    case CostedIntegerOperation::Multiply:
        if (query.bits > 64) return {};
        if (!wide && mips::selects_three_operand_multiply(subtarget)) {
            return {Instruction::Mul};
        }
        if (r6) return {Instruction::Dmul};
        return {wide ? Instruction::Dmult : Instruction::Mult,
                Instruction::Mflo};
    case CostedIntegerOperation::MultiplyHigh:
        if (query.bits != 32 && !wide) return {};
        if (r6) {
            return {query.is_signed ? (wide ? Instruction::Dmuh : Instruction::Muh)
                                    : (wide ? Instruction::Dmuhu : Instruction::Muhu)};
        }
        return {query.is_signed
                    ? (wide ? Instruction::Dmult : Instruction::Mult)
                    : (wide ? Instruction::Dmultu : Instruction::Multu),
                Instruction::Mfhi};
    case CostedIntegerOperation::Divide:
        if (query.bits != 32 && !wide) return {};
        if (r6) {
            return {query.is_signed ? (wide ? Instruction::Ddiv : Instruction::Div)
                                    : (wide ? Instruction::Ddivu : Instruction::Divu)};
        }
        return {query.is_signed
                    ? (wide ? Instruction::Ddiv : Instruction::Div)
                    : (wide ? Instruction::Ddivu : Instruction::Divu),
                Instruction::Mflo};
    }
    return {};
}

unsigned integer_register_bits(const Subtarget& subtarget) {
    return subtarget.has_feature(mips::Feature::Mips3) ? 64U : 32U;
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
        {"mips64r2", false, true, {"mips64"}, {}},
        // Release 6 re-encodes and removes instructions of earlier
        // releases, so it is a fact of the -march CPU rather than an
        // option; its FPU uses the IEEE 754-2008 NaN encoding.
        {"mips32r6", false, false, {"mips32r2", "nan2008"}, {}},
        {"mips64r6", false, false, {"mips64r2", "nan2008"}, {}},
        {"hard-float", false, true, {"mips1"}, {"soft-float"}},
        {"soft-float", false, true, {"mips1"}, {"hard-float"}},
        // Release 6 FPUs have only 64-bit registers. 64-bit FPRs need
        // 64-bit GPRs or MTHC1/MFHC1 (Release 2).
        {"fp32", false, true, {"hard-float"},
         {"fp64", "fpxx", "mips32r6", "mips64r6"}},
        {"fp64", false, true, {"hard-float"}, {"fp32", "fpxx"},
         {{"mips3", "mips32r2"}}},
        {"fpxx", false, true, {"hard-float", "mips2"}, {"fp32", "fp64"}},
        {"odd-spreg", false, true, {"hard-float"}, {}},
        {"llsc", false, true, {"mips2"}, {}},
        {"branch-likely", false, true, {"mips2"}, {"mips32r6", "mips64r6"}},
        {"fix4300", false, true, {"mips3", "hard-float"}, {}},
        {"abicalls", false, true, {"mips1"}, {}},
        // -mno-shared: -mabicalls code for an executable, which reaches
        // its own symbols directly (GCC's -mno-shared).
        {"shared", true, true, {"mips1"}, {}},
        {"mips16", false, true, {"mips1"}, {"micromips"}},
        {"micromips", false, true, {"mips32"}, {"mips16"}},
        {"single-float", false, true, {"hard-float"}, {"fp64", "fpxx"}},
        {"allegrex", false, false,
         {"mips2", "hard-float", "single-float"}, {}},
        {"cond-move", false, true, {"mips1"}, {"mips32r6", "mips64r6"}},
        {"rotate", false, true, {"mips1"}, {}},
        {"vfpu", false, true, {"allegrex"}, {}},
        // Pipeline interlocks are CPU facts rather than user-visible ISA
        // extensions.  The printer uses their absence to insert the exact
        // architectural separation required by older implementations.
        {"load-interlocks", false, false, {"mips1"}, {}},
        {"fpu-transfer-interlocks", false, false, {"mips1"}, {}},
        {"fpu-compare-interlocks", false, false, {"mips1"}, {}},
        {"hilo-interlocks", false, false, {"mips1"}, {}},
        // FCSR.NAN2008: Release 2 FPUs may implement the IEEE 754-2008 NaN
        // encoding; earlier ones have only the legacy encoding.
        {"nan2008", false, true, {}, {}, {{"mips32r2", "mips64r2"}}},
        // Errata workarounds, which the entries of the affected CPUs enable.
        {"fix-r4000", false, true, {"mips1"}, {}},
        {"fix-r4400", false, true, {"mips1"}, {}},
        {"fix-r10000", false, true, {"mips1"}, {}},
        {"fix-r5900", false, true, {"mips1"}, {}},
        {"fix-loongson3-llsc", false, true, {"mips1"}, {}},
        // Ordinary loads and stores take misaligned addresses, in hardware
        // or through the system's emulation, as Release 6 requires.
        {"unaligned-access", false, true, {"mips1"}, {}},
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
    boolean("mips64r2", false, "enable the MIPS64 Release 2 ISA");
    boolean("hard-float", true, "use hardware floating-point ABI endpoints");
    boolean("soft-float", false, "use integer floating-point ABI endpoints");
    boolean("fp32", true, "use the paired 32-bit FPR convention");
    boolean("fp64", false, "use 64-bit floating-point registers");
    boolean("fpxx", false, "use the o32 FPXX interoperable convention");
    boolean("odd-spreg", true, "allow odd single-precision FPRs");
    boolean("llsc", false, "enable load-linked/store-conditional atomics");
    boolean("branch-likely", false, "enable branch-likely instructions");
    boolean("fix4300", false, "work around the early VR4300 FP multiply erratum");
    boolean("fix-r4000", false,
            "work around the R4000 multiply and divide errata");
    boolean("fix-r4400", false, "work around the R4400 divide erratum");
    boolean("fix-r10000", false,
            "work around the R10000 LL/SC erratum with branch-likely retries");
    boolean("fix-r5900", false,
            "keep a NOP in the delay slot of a branch that ends a loop of "
            "six instructions or fewer (R5900 erratum)");
    boolean("fix-loongson3-llsc", false,
            "work around the Loongson 3 LL/SC erratum with SYNC before LL "
            "and at LL/SC exits");
    boolean("nan2008", false,
            "use the IEEE 754-2008 NaN encoding instead of the legacy one");
    boolean("unaligned-access", false,
            "access misaligned data with ordinary loads and stores");
    boolean("long-calls", false,
            "call through a register so the callee may lie outside the "
            "caller's 256 MB region");
    boolean("abicalls", false,
            "generate SVR4 abicalls code: calls through $t9 and symbol "
            "addresses from the GOT");
    boolean("shared", true,
            "with -mabicalls, make code suitable for shared objects; "
            "-mno-shared lets executables reach their own symbols directly");
    boolean("mips16", false, "enable MIPS16 encoding",
            OptionImplementation::Partial);
    boolean("micromips", false, "enable microMIPS encoding",
            OptionImplementation::Partial);
    boolean("single-float", false,
            "limit hardware floating point to single precision");
    boolean("cond-move", false, "enable conditional-move selection");
    boolean("rotate", false, "enable native variable rotates");
    boolean("vfpu", false, "enable the Allegrex VFPU extension",
            OptionImplementation::Partial);
    return result;
}

// The operand type that MIPS instruction entries name `name`.
std::optional<OperandType> operand_type(std::string_view name) {
    using Id = mips::OperandTypeId;
    const auto id = [](Id value) { return static_cast<std::uint16_t>(value); };
    const auto gpr = [&](std::vector<OperandWidth> widths) {
        OperandType result{OperandKind::Register, "integer", 0, 31};
        result.exposable = true;
        result.widths = std::move(widths);
        result.id = id(Id::Gpr);
        return result;
    };
    const auto fpr = [&](Id type, unsigned bits) {
        OperandType result{OperandKind::Register, "floating", 0, 31};
        result.exposable = true;
        result.widths = {{bits}};
        result.required_features = {"hard-float"};
        if (bits == 64) result.forbidden_features = {"single-float"};
        result.id = id(type);
        return result;
    };
    const auto number = [&](Id type, std::int64_t maximum) {
        OperandType result{OperandKind::Immediate, {}, 0, maximum};
        result.id = id(type);
        return result;
    };
    // 64-bit values in general registers need MIPS III.
    if (name == "gpr") return gpr({{32}, {64, "mips3"}});
    if (name == "gpr32") return gpr({{32}});
    if (name == "gpr64") return gpr({{64, "mips3"}});
    if (name == "fpr32") return fpr(Id::Fpr32, 32);
    if (name == "fpr64") return fpr(Id::Fpr64, 64);
    if (name == "cpr") return fpr(Id::Cpr, 32);
    if (name == "cpr64") return fpr(Id::Cpr64, 64);
    if (name == "fcc") return number(Id::Fcc, 7);
    if (name == "cp0" || name == "hwr" || name == "fcr") {
        auto result = number(name == "cp0" ? Id::Cp0 : name == "hwr" ? Id::Hwr : Id::Fcr, 31);
        result.exposable = true;
        result.register_syntax = true;
        if (name == "fcr") result.required_features = {"hard-float"};
        return result;
    }
    if (name == "imm") {
        OperandType result{OperandKind::Expression, {}, std::numeric_limits<std::int64_t>::min(),
                           std::numeric_limits<std::int64_t>::max()};
        result.id = id(Id::Expression);
        return result;
    }
    if (name == "branch" || name == "jump") {
        OperandType result{OperandKind::Label};
        result.relocated_bits = name == "branch" ? 16 : 26;
        // A branch built-in takes a label of its function.
        result.exposable = name == "branch";
        result.id = id(name == "branch" ? Id::Branch : Id::Jump);
        return result;
    }
    // Release 6 compact branches and PC-relative operands.
    for (const auto& [spelling, type, bits] :
         {std::tuple{std::string_view{"branch21"}, Id::Branch21, 21U},
          std::tuple{std::string_view{"branch26"}, Id::Branch26, 26U},
          std::tuple{std::string_view{"pcrel19s2"}, Id::PcRelative19, 19U},
          std::tuple{std::string_view{"pcrel18s3"}, Id::PcRelative18, 18U}}) {
        if (name != spelling) continue;
        OperandType result{OperandKind::Label};
        result.relocated_bits = bits;
        result.id = id(type);
        return result;
    }
    const auto parse = [](std::string_view text) -> std::optional<std::int64_t> {
        std::int64_t value{};
        const auto* end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
        return value;
    };
    // memN: a GPR base and a signed 16-bit offset, for an N-bit access;
    // memN_simmK: a signed K-bit offset, which takes no relocation.
    if (name.starts_with("mem")) {
        auto spelling = name.substr(3);
        std::int64_t offset_bits = 16;
        if (const auto suffix = spelling.find("_simm"); suffix != std::string_view::npos) {
            const auto width = parse(spelling.substr(suffix + 5));
            if (!width || *width < 1 || *width > 16) return std::nullopt;
            offset_bits = *width;
            spelling = spelling.substr(0, suffix);
        }
        const auto bits = spelling.empty() ? std::optional<std::int64_t>(0) : parse(spelling);
        if (!bits || (*bits != 0 && *bits != 8 && *bits != 16 && *bits != 32 && *bits != 64)) {
            return std::nullopt;
        }
        OperandType result{OperandKind::Memory, "integer", 0, 31,
                           offset_bits == 16 ? 16U : 0U,
                           -(std::int64_t{1} << (offset_bits - 1)),
                           (std::int64_t{1} << (offset_bits - 1)) - 1,
                           static_cast<unsigned>(*bits)};
        result.exposable = true;
        result.id = id(Id::Memory);
        return result;
    }
    // uimmN and simmN, and LO..HI; 16-bit fields take relocations.
    OperandType result{OperandKind::Immediate};
    result.exposable = true;
    result.id = id(Id::Immediate);
    if (const auto dots = name.find(".."); dots != std::string_view::npos) {
        const auto low = parse(name.substr(0, dots));
        const auto high = parse(name.substr(dots + 2));
        if (!low || !high || *low > *high) return std::nullopt;
        result.minimum = *low;
        result.maximum = *high;
        return result;
    }
    const bool is_signed = name.starts_with("simm");
    if (!is_signed && !name.starts_with("uimm")) return std::nullopt;
    const auto bits = parse(name.substr(4));
    if (!bits || *bits < 1 || *bits > 32) return std::nullopt;
    result.minimum = is_signed ? -(std::int64_t{1} << (*bits - 1)) : 0;
    result.maximum = is_signed ? (std::int64_t{1} << (*bits - 1)) - 1
                               : (std::int64_t{1} << *bits) - 1;
    result.relocated_bits = *bits == 16 ? 16 : 0;
    return result;
}

// The number of a general or floating register, from its storage.
std::optional<unsigned> register_number(std::string_view name) {
    static const auto registers = target_registers();
    for (const auto& reg : registers) {
        if (reg.name != name) continue;
        const auto storage = reg.storage;
        if (storage.size() < 2 || (storage[0] != 'r' && storage[0] != 'f')) return std::nullopt;
        unsigned value{};
        const auto parsed =
            std::from_chars(storage.data() + 1, storage.data() + storage.size(), value);
        if (parsed.ec != std::errc{}) return std::nullopt;
        return value;
    }
    return std::nullopt;
}

const InstructionModel instruction_model{
    32,
    operand_type,
    register_number,
    {"load", "coprocessor-load", "from-coprocessor", "to-coprocessor", "coprocessor",
     "condition-read", "condition-write", "no-delay-slot"},
    {{"li", {OperandKind::Register, OperandKind::Expression},
      {"addiu", "ori", "lui", "dsll", "dsrl32"}},
     {"dli", {OperandKind::Register, OperandKind::Expression},
      {"addiu", "ori", "lui", "dsll", "dsrl32"}}},
    "at",
    {
#define X(name, mnemonic, ...) SelectedInstruction{mnemonic, {__VA_ARGS__}},
#include "target/mips/instructions.def"
#undef X
    },
};

TargetInfo make_target(ByteOrder order, std::vector<TriplePrefix> prefixes) {
    TargetInfo result{
        "mips", std::move(prefixes),
        {order, 8, 16, 8,
         order == ByteOrder::Big
             ? BitFieldOrder::MostSignificantFirst
             : BitFieldOrder::LeastSignificantFirst,
         BitFieldUnitSharing::SameUnqualifiedBase,
         BitFieldPlacement::AlignedUnits, CodeAddressRepresentation::Flat,
         // R4000-class FPUs, the VR4300 among them, trap on a quiet NaN
         // operand, and other legacy FPUs choose their own result; FPUs with
         // the 2008 encoding propagate a NaN operand.
         floating::NanEncoding{
             .legacy = true,
             .propagation = {floating::NanPropagation::Unimplemented,
                             floating::NanPropagation::Unimplemented,
                             floating::NanPropagation::Unimplemented,
                             floating::NanPropagation::Unimplemented}},
         "nan2008",
         floating::NanEncoding{
             .propagation = {floating::NanPropagation::FirstSignaling,
                             floating::NanPropagation::FirstSignaling,
                             floating::NanPropagation::FirstSignaling,
                             floating::NanPropagation::FirstSignaling}}},
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
        {},
        {{32, "llsc"}},
        {},
        target_options(),
        &subtargets,
        {integer_constant_materialization_cost,
         integer_operation_instructions, mips::bit_operation_instructions,
         integer_register_bits, mips::select_instructions},
        {{0, 0, 0, 0, true, true, true, true, false, true, true}},
        // Instruction memory operands are a GPR base plus a signed 16-bit
        // displacement; MIPS has no scaled index.
        {{32, 64, "integer", {0}, {}, {}, 16, "mips"},
         {64, 64, "integer", {0}, {}, {}, 16, "mips3"}},
        {{"integer128", {}}, {"atomics", "m.llsc"}, {"thread_local", {}}},
        {{"eabi32", 32}},
        target_hazard_events(),
        NakedLowering::Constrained,
        {{"$::sqrt", "$::_sqrt"}},
    };
    // The models give the instruction table.
    result.instruction_model = &instruction_model;
    // %gp_rel offsets are relative to _gp, which $gp holds.
    result.data_base_registers = {"gp"};
    // 64-bit integers are native from MIPS III and word pairs before it.
    result.selected_integer_bits = 64;
    result.integrated_assembler = &mips::assemble;
    result.elf_machine = mips::em_mips;
    result.linker = &mips::make_linker_target;
    return result;
}

// mips64 triples produce ELF64 objects with 64-bit addresses. Not const:
// loading the models installs their instruction table.
TargetInfo big_endian = make_target(
    ByteOrder::Big, {{"mipsallegrex", 32}, {"mips64", 64}, {"mips", 32}});
TargetInfo little_endian = make_target(
    ByteOrder::Little,
    {{"mipsallegrexel", 32}, {"mips64el", 64}, {"mipsel", 32}});

} // namespace

const TargetInfo& mips_target() { return big_endian; }
const TargetInfo& mipsel_target() { return little_endian; }

} // namespace cross
