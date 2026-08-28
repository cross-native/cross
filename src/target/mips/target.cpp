// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/target.hpp"

#include "common/options.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace cross {
namespace {

std::vector<RegisterEntry> target_registers() {
    std::vector<RegisterEntry> result;
    const auto gpr = [&](std::string_view name, std::string_view storage,
                         bool compiler_owned = false) {
        result.push_back({
            name, storage, 64, "integer", "mips1", true,
            compiler_owned
                ? std::vector<RegisterEntry::ScalarMode>{}
                : std::vector<RegisterEntry::ScalarMode>{
                      {8, false}, {16, false}, {32, false}, {64, false}},
            compiler_owned});
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
    gpr("k0", "r26", true); gpr("k1", "r27", true);
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
        result.push_back({names[index], storage[index], 64, "floating",
                          "hard-float", false, std::move(modes), false});
    }
    result.push_back({"hi", "hi", 64, "special", "mips1", false,
                      {}, true});
    result.push_back({"lo", "lo", 64, "special", "mips1", false,
                      {}, true});
    return result;
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
        {order, 8, 16, 8},
        target_registers(),
        {
            {"i8", 8, "mips1"}, {"u8", 8, "mips1"},
            {"i16", 16, "mips1"}, {"u16", 16, "mips1"},
            {"i32", 32, "mips1"}, {"u32", 32, "mips1"},
            // MIPS I/II use target-legalized 32-bit GPR pairs; MIPS III and
            // later select native 64-bit GPR operations for the same types.
            {"i64", 64, "mips1"}, {"u64", 64, "mips1"},
            {"iptr", 32, "mips1"}, {"uptr", 32, "mips1"},
        },
        {},
        {{32, "llsc"}},
        {},
        target_options(),
        &subtargets,
    };
}

const TargetInfo big_endian = make_target(
    ByteOrder::Big, {"mipsallegrex", "mips"});
const TargetInfo little_endian = make_target(
    ByteOrder::Little, {"mipsallegrexel", "mipsel"});

} // namespace

const TargetInfo& mips_target() { return big_endian; }
const TargetInfo& mipsel_target() { return little_endian; }

} // namespace cross
