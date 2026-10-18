// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/assembler.hpp"

#include "assembler/assembler.hpp"
#include "model/model.hpp"
#include "target/mips/elf.hpp"
#include "target/mips/features.hpp"
#include "target/mips/instructions.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <limits>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cross::mips {
namespace {

using assembler::Assembler;
using assembler::Cursor;
using assembler::ExpressionId;
using assembler::FixupKind;
using assembler::Statement;
using assembler::SymbolId;
namespace elf = assembler::elf;

// ISA levels as bits, for the ELF header and .MIPS.abiflags.
enum : std::uint16_t {
    isa_mips1 = 1U << 0,
    isa_mips2 = 1U << 1,
    isa_mips3 = 1U << 2,
    isa_mips4 = 1U << 3,
    isa_mips5 = 1U << 4,
    isa_mips32 = 1U << 5,
    isa_mips32r2 = 1U << 6,
    isa_mips64 = 1U << 7,
    isa_mips64r2 = 1U << 8,
};
// The levels with 64-bit general registers.
constexpr std::uint16_t isa_64bit = isa_mips3 | isa_mips4 | isa_mips5 | isa_mips64 | isa_mips64r2;

struct IsaLevel {
    std::string_view name;
    std::uint16_t bit;
    std::uint8_t level;
    std::uint8_t revision;
    std::uint32_t elf_arch;
};

constexpr std::array isa_levels{
    IsaLevel{"mips1", isa_mips1, 1, 0, 0x00000000},
    IsaLevel{"mips2", isa_mips2, 2, 0, 0x10000000},
    IsaLevel{"mips3", isa_mips3, 3, 0, 0x20000000},
    IsaLevel{"mips4", isa_mips4, 4, 0, 0x30000000},
    IsaLevel{"mips5", isa_mips5, 5, 0, 0x40000000},
    IsaLevel{"mips32", isa_mips32, 32, 1, 0x50000000},
    IsaLevel{"mips32r2", isa_mips32r2, 32, 2, 0x70000000},
    IsaLevel{"mips64", isa_mips64, 64, 1, 0x60000000},
    IsaLevel{"mips64r2", isa_mips64r2, 64, 2, 0x80000000},
};

const IsaLevel& isa_level(std::uint16_t bit) {
    for (const auto& level : isa_levels) {
        if (level.bit == bit) return level;
    }
    return isa_levels.front();
}

enum : std::uint16_t {
    fixup_hi16 = FixupKind::target_base,
    fixup_lo16,
    fixup_gprel16,
    fixup_higher,
    fixup_highest,
    fixup_jump26,
    fixup_branch16,
    // A plain 16-bit immediate that only layout makes absolute.
    fixup_signed16,
    fixup_unsigned16,
};

enum class Abi : std::uint8_t { O32, N64, Eabi32 };

// The pipeline interlocks of a CPU: hazards that `.set reorder` need not
// separate with nops. They are the CPU model's facts, which the code
// generator also follows.
struct Interlocks {
    bool load{};      // loads, and coprocessor 0 moves
    bool transfer{};  // FPU and FPU control register moves
    bool compare{};   // an FPU compare and its condition's reader
    bool hilo{};      // a HI/LO read and a following write
};

// Features as bits by mips::Feature, the order of the target's feature
// table.
using FeatureBits = std::uint64_t;
static_assert(static_cast<unsigned>(Feature::Count) <= 64);

constexpr FeatureBits bit(Feature feature) {
    return FeatureBits{1} << static_cast<unsigned>(feature);
}

constexpr FeatureBits level_features =
    bit(Feature::Mips1) | bit(Feature::Mips2) | bit(Feature::Mips3) | bit(Feature::Mips4) |
    bit(Feature::Mips5) | bit(Feature::Mips32) | bit(Feature::Mips32r2) |
    bit(Feature::Mips64) | bit(Feature::Mips64r2);

// The bit of the feature named `name`; 0 for none.
FeatureBits feature_bit(const SubtargetTable& table, std::string_view name) {
    for (std::size_t index = 0; index < table.features.size(); ++index) {
        if (table.features[index].name == name) return FeatureBits{1} << index;
    }
    return 0;
}

// The features with the features they imply.
FeatureBits with_prerequisites(const SubtargetTable& table, FeatureBits bits) {
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < table.features.size(); ++index) {
            if ((bits & (FeatureBits{1} << index)) == 0) continue;
            for (const auto name : table.features[index].prerequisites) {
                const auto implied = feature_bit(table, name);
                if ((bits & implied) == 0) {
                    bits |= implied;
                    changed = true;
                }
            }
        }
    }
    return bits;
}

// What `.set mipsN`, `.set arch=`, and `.module arch=` select: the
// features that decide which instructions are accepted, and the ISA level,
// odd single-precision registers, and interlocks they give.
struct Architecture {
    FeatureBits features{bit(Feature::Mips1)};
    std::uint16_t isa{isa_mips1};
    bool odd_single{};
    Interlocks interlocks{};
};

Architecture architecture_from(FeatureBits features) {
    const auto has = [&](Feature feature) { return (features & bit(feature)) != 0; };
    Architecture result;
    result.features = features;
    result.isa = has(Feature::Mips64r2)   ? isa_mips64r2
                 : has(Feature::Mips64)   ? isa_mips64
                 : has(Feature::Mips32r2) ? isa_mips32r2
                 : has(Feature::Mips32)   ? isa_mips32
                 : has(Feature::Mips5)    ? isa_mips5
                 : has(Feature::Mips4)    ? isa_mips4
                 : has(Feature::Mips3)    ? isa_mips3
                 : has(Feature::Mips2)    ? isa_mips2
                                          : isa_mips1;
    result.odd_single = has(Feature::OddSpreg);
    result.interlocks = {has(Feature::LoadInterlocks), has(Feature::FpuTransferInterlocks),
                         has(Feature::FpuCompareInterlocks), has(Feature::HiloInterlocks)};
    return result;
}

// `architecture` with the ISA level `level`, without CPU extensions such
// as Allegrex's unless `extensions`.
Architecture with_level(const SubtargetTable& table, const Architecture& architecture,
                        std::string_view level, bool extensions) {
    auto features = architecture.features & ~level_features;
    if (!extensions) features &= ~bit(Feature::Allegrex);
    return architecture_from(features | with_prerequisites(table, feature_bit(table, level)));
}

// The model CPU entry named `name`.
std::optional<Architecture> cpu_architecture(const Subtarget& subtarget, std::string_view name) {
    const auto& registry = model_registry();
    const auto* entry = registry.find_cpu(subtarget.target().architecture, name);
    if (!entry) return std::nullopt;
    const auto& table = *subtarget.target().subtargets;
    FeatureBits features{};
    for (const auto& feature : registry.resolve_cpu(subtarget.target(), *entry).features) {
        features |= feature_bit(table, feature);
    }
    return architecture_from(with_prerequisites(table, features));
}

bool has_64bit_registers(std::uint16_t isa) { return (isa & isa_64bit) != 0; }

// The file-level floating-point convention.
enum class FprMode : std::uint8_t { Fp32, Fpxx, Fp64 };

struct Model {
    bool elf64{};
    bool big_endian{};
    Abi abi{};
    Architecture architecture;
    // The ISA has 64-bit GPRs; the file assumes 64-bit GPRs for macros and
    // .MIPS.abiflags.
    bool isa64{};
    bool gp64{};
    // .MIPS.abiflags facts: Val_GNU_MIPS_ABI_FP_*, AFL_REG_* of the FPRs,
    // and AFL_FLAGS1_ODDSPREG.
    std::uint8_t fp_abi{1};
    std::uint8_t cpr1_size{1};
    bool odd_spreg{};
    // 64-bit FPRs (FR=1): doubles may use odd registers.
    bool fr1{};
    bool nan2008{};
    bool soft_float{};
    bool single_float{};
    FprMode fpr_mode{};
    // The input is not the compiler's own output: object conventions
    // follow GNU as rather than the LLVM MC conventions of `cc -c`.
    bool gnu{};
};

bool doubleword_fpu_memory(const Subtarget& subtarget) {
    if (!subtarget.has_feature(Feature::Mips2)) return false;
    const auto load = find_instruction_cost(subtarget.target(), "ldc1");
    const auto store = find_instruction_cost(subtarget.target(), "sdc1");
    return load && store && subtarget.has_instruction(*load) &&
           subtarget.has_instruction(*store);
}

// The width of the ABI's integer registers.
unsigned abi_gpr_bits(const AbiEntry& abi) {
    for (const auto& bank : abi.banks) {
        if (bank.register_class == "integer" && bank.register_bits != 0) return bank.register_bits;
    }
    return abi.address_bits;
}

// .MIPS.abiflags FP ABI facts of GNU as for the file-level convention.
void gnu_floating_abi(Model& model) {
    if (model.soft_float) {
        model.fp_abi = 3;
        model.cpr1_size = 0;
    } else {
        model.cpr1_size = model.fpr_mode == FprMode::Fp64 ? 2 : 1;
        if (model.single_float) {
            model.fp_abi = 2;
        } else if (model.fpr_mode == FprMode::Fpxx) {
            model.fp_abi = 5;
        } else if (model.fpr_mode == FprMode::Fp64) {
            model.fp_abi = model.gp64 ? 1 : model.odd_spreg ? 6 : 7;
        } else {
            model.fp_abi = model.gp64 ? 0 : 1;
        }
    }
    model.fr1 = model.fpr_mode == FprMode::Fp64;
}

Model model_for(const Subtarget& subtarget, bool compiler_output) {
    const auto has = [&](Feature feature) { return subtarget.has_feature(feature); };
    Model model;
    model.elf64 = subtarget.abi_info().address_bits > 32;
    model.big_endian = subtarget.target().data_layout.byte_order == ByteOrder::Big;
    model.abi = model.elf64 ? Abi::N64
                : subtarget.abi_info().elf_abi_tag == "eabi32" ? Abi::Eabi32
                                                               : Abi::O32;
    FeatureBits features{};
    for (unsigned index = 0; index < static_cast<unsigned>(Feature::Count); ++index) {
        if (has(static_cast<Feature>(index))) features |= FeatureBits{1} << index;
    }
    model.architecture = architecture_from(features);
    model.isa64 = has(Feature::Mips3);
    model.nan2008 = has(Feature::Nan2008);
    model.soft_float = has(Feature::SoftFloat);
    model.single_float = has(Feature::SingleFloat);
    model.gnu = !compiler_output;
    if (model.gnu) {
        // GNU as assumes the narrower of the ISA's and the ABI's GPRs, and
        // 64-bit FPRs with 64-bit GPRs.
        model.gp64 = model.isa64 && abi_gpr_bits(subtarget.abi_info()) == 64;
        model.fpr_mode = model.gp64 || has(Feature::Fp64) ? FprMode::Fp64
                         : has(Feature::Fpxx)            ? FprMode::Fpxx
                                                         : FprMode::Fp32;
        model.odd_spreg = model.fpr_mode != FprMode::Fpxx && !model.soft_float;
        gnu_floating_abi(model);
        return model;
    }
    model.gp64 = model.isa64;
    model.odd_spreg = true;
    if (model.soft_float) {
        model.fp_abi = 3;
        model.cpr1_size = 0;
    } else if (model.elf64) {
        // The n64 object class is defined on a 64-bit FPU.
        model.cpr1_size = 2;
        model.fr1 = true;
    } else if (model.single_float) {
        model.fp_abi = 2;
    } else if (has(Feature::Fp64)) {
        // With 64-bit FPRs every register holds a single, as GNU as assumes.
        model.cpr1_size = 2;
        model.fr1 = true;
        model.fp_abi = 6;
    } else if (has(Feature::Fpxx) ||
               (has(Feature::Fp32) && doubleword_fpu_memory(subtarget) &&
                !has(Feature::OddSpreg))) {
        // Compiled code keeps to the FPXX subset of the paired-register
        // convention, so it links with either FPU. An FPU with odd single
        // registers colors them under -mfp32, and without ldc1 and sdc1, as
        // on MIPS I, f64 halves travel through odd single registers: such
        // code stays FP32.
        model.fp_abi = 5;
        model.odd_spreg = false;
    }
    model.fpr_mode = model.fr1 ? FprMode::Fp64 : model.fp_abi == 5 ? FprMode::Fpxx : FprMode::Fp32;
    return model;
}

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool is_int16(std::int64_t value) { return value >= -32768 && value <= 32767; }
bool is_uint16(std::int64_t value) { return value >= 0 && value <= 65535; }

// GPR names of the o32 and EABI conventions; n64 renames 8-15.
std::optional<unsigned> gpr_number(std::string_view name, bool n64) {
    unsigned number = 0;
    if (!name.empty() && std::all_of(name.begin(), name.end(),
                                     [](char c) { return c >= '0' && c <= '9'; })) {
        if (name.size() > 2) return std::nullopt;
        for (const char c : name) number = number * 10 + static_cast<unsigned>(c - '0');
        if (number > 31) return std::nullopt;
        return number;
    }
    static const std::unordered_map<std::string_view, unsigned> common{
        {"zero", 0}, {"at", 1}, {"v0", 2}, {"v1", 3}, {"a0", 4}, {"a1", 5},
        {"a2", 6}, {"a3", 7}, {"s0", 16}, {"s1", 17}, {"s2", 18}, {"s3", 19},
        {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23}, {"t8", 24}, {"t9", 25},
        {"k0", 26}, {"k1", 27}, {"gp", 28}, {"sp", 29}, {"fp", 30}, {"s8", 30},
        {"ra", 31}};
    static const std::unordered_map<std::string_view, unsigned> o32{
        {"t0", 8}, {"t1", 9}, {"t2", 10}, {"t3", 11}, {"t4", 12}, {"t5", 13},
        {"t6", 14}, {"t7", 15}};
    static const std::unordered_map<std::string_view, unsigned> new_abi{
        {"a4", 8}, {"a5", 9}, {"a6", 10}, {"a7", 11}, {"ta0", 8}, {"ta1", 9},
        {"ta2", 10}, {"ta3", 11}, {"t0", 12}, {"t1", 13}, {"t2", 14}, {"t3", 15}};
    if (const auto found = common.find(name); found != common.end()) return found->second;
    const auto& table = n64 ? new_abi : o32;
    if (const auto found = table.find(name); found != table.end()) return found->second;
    return std::nullopt;
}

std::optional<unsigned> small_number(std::string_view text, unsigned limit) {
    if (text.empty() || text.size() > 2) return std::nullopt;
    unsigned value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value > limit) return std::nullopt;
    return value;
}

// The registers an instruction reads and writes and the properties the
// `.set reorder` hazard and delay-slot rules use, as GNU as's opcode table
// records them.
namespace effect {
inline constexpr std::uint32_t load_memory = 1U << 0;
inline constexpr std::uint32_t load_coprocessor = 1U << 1;
inline constexpr std::uint32_t coprocessor_move = 1U << 2;
inline constexpr std::uint32_t coprocessor_load = 1U << 3;
inline constexpr std::uint32_t write_condition = 1U << 4;
inline constexpr std::uint32_t read_condition = 1U << 5;
inline constexpr std::uint32_t read_hi = 1U << 6;
inline constexpr std::uint32_t read_lo = 1U << 7;
inline constexpr std::uint32_t write_hi = 1U << 8;
inline constexpr std::uint32_t write_lo = 1U << 9;
inline constexpr std::uint32_t delayed = 1U << 10;
inline constexpr std::uint32_t unconditional = 1U << 11;
inline constexpr std::uint32_t likely = 1U << 12;
inline constexpr std::uint32_t no_delay_slot = 1U << 13;
inline constexpr std::uint32_t coprocessor = 1U << 14;
inline constexpr std::uint32_t cp0 = 1U << 15;
inline constexpr std::uint32_t any = ~0U;
} // namespace effect

struct Effects {
    // GPR masks never include $0.
    std::uint32_t gpr_read{};
    std::uint32_t gpr_write{};
    std::uint32_t fpr_read{};
    std::uint32_t fpr_write{};
    std::uint32_t flags{};
};

struct Immediate {
    std::optional<std::uint16_t> relocation;
    ExpressionId expression{};
    SourceLocation location;
};

struct PendingFixup {
    std::uint16_t kind{};
    ExpressionId expression{};
    SourceLocation location;
};

// The registers and flags of an instruction's effects, by operand.
struct EffectPlan {
    std::vector<unsigned> gpr_reads;
    std::vector<unsigned> gpr_writes;
    std::vector<unsigned> fpr_reads;
    std::vector<unsigned> fpr_writes;
    // Memory operands, whose base the instruction reads.
    std::vector<unsigned> bases;
    std::uint32_t fixed_gpr_reads{};
    std::uint32_t fixed_gpr_writes{};
    std::uint32_t flags{};
    // An operand holds a double or long, which names both registers of a
    // pair with 32-bit FPRs.
    bool wide{};
};

OperandTypeId type_id(const ModelOperand& operand) {
    return static_cast<OperandTypeId>(operand.type.id);
}

bool floating_type(OperandTypeId id) {
    return id == OperandTypeId::Fpr32 || id == OperandTypeId::Fpr64 ||
           id == OperandTypeId::Cpr || id == OperandTypeId::Cpr64;
}

bool wide_type(OperandTypeId id) {
    return id == OperandTypeId::Fpr64 || id == OperandTypeId::Cpr64;
}

EffectPlan effect_plan(const ModelInstruction& entry) {
    using namespace effect;
    EffectPlan plan;
    const auto add = [&](const std::vector<ModelEffect>& effects, bool writes) {
        for (const auto& item : effects) {
            switch (item.kind) {
            case ModelEffect::Kind::Operand: {
                const auto id = type_id(entry.operands[item.operand]);
                auto& list = floating_type(id) ? (writes ? plan.fpr_writes : plan.fpr_reads)
                                               : (writes ? plan.gpr_writes : plan.gpr_reads);
                list.push_back(item.operand);
                break;
            }
            case ModelEffect::Kind::Register:
                if (item.name == "hi") {
                    plan.flags |= writes ? write_hi : read_hi;
                } else if (item.name == "lo") {
                    plan.flags |= writes ? write_lo : read_lo;
                } else if (const auto number = gpr_number(item.name, false)) {
                    (writes ? plan.fixed_gpr_writes : plan.fixed_gpr_reads) |=
                        (1U << *number) & ~1U;
                }
                break;
            case ModelEffect::Kind::Resource:
                if (item.name == "cp0") plan.flags |= cp0;
                break;
            }
        }
    };
    add(entry.reads, false);
    add(entry.writes, true);
    for (const auto& hazard : entry.hazards) {
        plan.flags |= hazard == "load"               ? load_memory
                      : hazard == "coprocessor-load" ? coprocessor_load
                      : hazard == "from-coprocessor" ? load_coprocessor
                      : hazard == "to-coprocessor"   ? coprocessor_move
                      : hazard == "coprocessor"      ? coprocessor
                      : hazard == "condition-read"   ? read_condition
                      : hazard == "condition-write"  ? write_condition
                                                     : no_delay_slot;
    }
    if (entry.delay_slot) {
        plan.flags |= delayed;
        if (entry.control == InstructionControlEffect::UnconditionalBranch ||
            entry.control == InstructionControlEffect::RawReturn) {
            plan.flags |= unconditional;
        }
    }
    if (entry.likely) plan.flags |= likely;
    for (unsigned index = 0; index < entry.operands.size(); ++index) {
        const auto id = type_id(entry.operands[index]);
        if (id == OperandTypeId::Memory) plan.bases.push_back(index);
        plan.wide = plan.wide || wide_type(id);
    }
    return plan;
}

// A form of a mnemonic: the entry as spelled, and the entry that encodes
// it, which is an alias's target.
struct Form {
    const ModelInstruction* spelled{};
    const ModelInstruction* entry{};
    // The CPU has the form with any of `features`, or always without any,
    // unless it has one of `excludes`.
    FeatureBits features{};
    FeatureBits excludes{};
    EffectPlan effects;
};

// An operand's value: a register number or an immediate, a memory
// operand's base, or a value that a fixup gives.
struct OperandValue {
    std::int64_t value{};
    std::int64_t base{};
    // The value of a macro's expression operand.
    ExpressionId expression{};
    std::optional<PendingFixup> fixup;
    // An FPR written as a coprocessor register number, `$N`.
    bool numeric{};
    SourceLocation location;
};

class MipsTarget final : public assembler::Target {
public:
    MipsTarget(const Model& model, const Subtarget& subtarget, unsigned optimize)
        : model_(model), subtarget_(subtarget), optimize_(optimize) {
        file_.architecture = model.architecture;
        file_.gp64 = model.gp64;
        file_.fpr = model.fpr_mode;
        file_.odd_spreg = model.odd_spreg;
        options_ = file_;
        // The forms of each mnemonic in the loaded instruction table.
        const auto& table = *model_registry().instruction_table(subtarget.target().architecture);
        const auto& features = *subtarget.target().subtargets;
        std::vector<EffectPlan> plans;
        plans.reserve(table.entries.size());
        for (const auto& entry : table.entries) {
            plans.push_back(entry.encoded ? effect_plan(entry) : EffectPlan{});
        }
        for (const auto& [mnemonic, indices] : table.forms) {
            auto& forms = forms_[mnemonic];
            for (const auto index : indices) {
                const auto& spelled = table.entries[index];
                const auto encoded = spelled.is_alias() ? spelled.alias_entry : index;
                const auto& entry = table.entries[encoded];
                Form form{&spelled, &entry, 0, 0, plans[encoded]};
                for (const auto& name : entry.features) form.features |= feature_bit(features, name);
                for (const auto& name : entry.excludes) form.excludes |= feature_bit(features, name);
                forms.push_back(std::move(form));
            }
        }
        for (const auto& [mnemonic, forms] : forms_) {
            for (const auto& form : forms) {
                named_.emplace(form.spelled->name, &form);
                if (form.spelled == form.entry) encoded_forms_.emplace(form.entry, &form);
            }
        }
        nan2008_ = model.nan2008;
    }

    [[nodiscard]] assembler::ObjectInfo object() const override {
        return {model_.elf64, model_.big_endian, 8, model_.elf64};
    }

    [[nodiscard]] std::uint32_t elf_flags() const override {
        std::uint32_t flags = isa_level(model_.architecture.isa).elf_arch;
        if (model_.abi == Abi::O32) flags |= ef_mips_abi_o32;
        if (model_.abi == Abi::Eabi32) flags |= ef_mips_abi_eabi32;
        if (model_.isa64 && !model_.elf64 && (!model_.gnu || model_.abi == Abi::O32)) {
            flags |= ef_mips_32bitmode;
        }
        if (noreorder_) flags |= ef_mips_noreorder;
        if (pic_) flags |= ef_mips_pic | ef_mips_cpic;
        if (nan2008_) flags |= ef_mips_nan2008;
        return flags;
    }

    [[nodiscard]] assembler::FrameFacts frame_facts() const override {
        assembler::FrameFacts facts;
        facts.code_alignment = 1;
        facts.data_alignment = model_.elf64 ? -8 : -4;
        facts.return_address = 31;
        facts.initial_instructions = {0x0d, 29}; // DW_CFA_def_cfa_register $sp
        facts.initial_cfa_offset = 0;
        facts.eh_pointer_encoding = model_.elf64 ? 0x0c : 0x0b; // sdata8/sdata4
        return facts;
    }

    [[nodiscard]] std::uint32_t section_type(std::string_view name,
                                             std::uint32_t type) const override {
        if (name.starts_with(".debug_")) return sht_mips_dwarf;
        return type;
    }

    [[nodiscard]] std::uint64_t section_flags(std::string_view name,
                                              std::uint64_t flags) const override {
        // GNU as marks the sections that $gp addresses.
        const auto family = [&](std::string_view prefix) {
            return name == prefix || (name.starts_with(prefix) && name[prefix.size()] == '.');
        };
        if (model_.gnu && (family(".sdata") || family(".sbss") || name == ".lit4" ||
                           name == ".lit8" || name == ".srdata" || name == ".got")) {
            flags |= shf_mips_gprel;
        }
        return flags;
    }

    // GCC's `$L` labels: every `$` name is private in a 32-bit object, as in
    // GNU as and, for o32, LLVM MC.
    [[nodiscard]] bool private_label(std::string_view name) const override {
        return !model_.elf64 && name.starts_with('$');
    }

    [[nodiscard]] bool line_table_without_rows() const override { return !model_.gnu; }

    // GNU as aligns a local common object as its size suggests.
    [[nodiscard]] std::uint64_t local_common_alignment(std::uint64_t size) const override {
        if (!model_.gnu) return 1;
        return size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
    }

    [[nodiscard]] std::optional<unsigned> dwarf_register(
        std::string_view name) const override {
        if (!name.starts_with("$")) return std::nullopt;
        name.remove_prefix(1);
        if (name.starts_with("f") && !name.starts_with("fp")) {
            if (const auto number = small_number(name.substr(1), 31)) return 32 + *number;
            return std::nullopt;
        }
        return gpr_number(name, model_.elf64);
    }

    [[nodiscard]] assembler::FixupInfo fixup_info(FixupKind kind) const override {
        return {4, kind.value == fixup_branch16};
    }

    void instruction(Assembler& as, Statement& statement) override;
    bool directive(Assembler& as, Statement& statement) override;
    void label(Assembler&, SymbolId id) override { labels_.push_back(id); }
    void flush(Assembler& as) override;
    void finish(Assembler& as) override;
    bool apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                     std::span<std::uint8_t> bytes, bool relocated) const override;
    std::optional<assembler::RelocationChoice> relocation(
        Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
        const assembler::Symbol& symbol, const assembler::Section* symbol_section) const override;
    void order_relocations(std::vector<assembler::Relocation>& relocations) const override;
    // GNU as keeps relocations in the order of their fixups and moves each
    // HI16 that its next fixup does not match before the LO16 it pairs
    // with, the most recent first.
    static void gnu_order(std::vector<assembler::Relocation>& relocations);

private:
    struct Settings {
        Architecture architecture;
        bool gp64{};
        FprMode fpr{};
        // The odd single-precision option; odd_singles() applies it.
        bool odd_spreg{};
        bool noreorder{};
        bool at{true};
        bool macro{true};
        // `.set nomove` (`.set volatile`) keeps instructions in place.
        bool move{true};
    };

    // An instruction in the reorder history: its effects, whether it may
    // move into a delay slot, and where it is. A placeholder is a nop.
    // The errata fixes also note GNU as's facts: whether an unconditional
    // jump cleared the entry, an LL, SC, or SYNC, and a branch's target.
    enum class Sequence : std::uint8_t { None, LoadLinked, StoreConditional, Sync };
    struct Emitted {
        Effects effects;
        bool fixed{true};
        bool noreorder{};
        bool placed{};
        assembler::SectionId section{};
        std::uint32_t fragment{};
        std::size_t offset{};
        bool has_fixup{};
        bool cleared{};
        Sequence sequence{};
        std::optional<SymbolId> target;
    };
    static constexpr std::size_t history_size = 5;
    static constexpr std::size_t max_delay_nops = 2;

    // Operands.
    std::optional<unsigned> gpr(Assembler& as, Cursor& cursor);
    // An FPR operand. `$N` names one only in loads, stores, and moves, where
    // GNU as reads it as a coprocessor register: such an operand counts in
    // no FPR mask, hazard, or pairing rule, which `numeric_fpr_` records.
    std::optional<unsigned> fpr(Assembler& as, Cursor& cursor, bool numeric = false);
    std::optional<unsigned> condition_code(Cursor& cursor);
    bool comma(Assembler& as, Cursor& cursor);
    std::optional<Immediate> immediate(Assembler& as, Cursor& cursor);
    // A 16-bit field's value: in range, or a fixup for a relocation
    // operator or a value that layout settles.
    bool sixteen(Assembler& as, const Immediate& value, std::int64_t minimum,
                 std::int64_t maximum, OperandValue& result);
    std::optional<std::int64_t> absolute(Assembler& as, Cursor& cursor, std::int64_t low,
                                         std::int64_t high, std::string_view what);

    // Emission. `append` places an instruction as GNU as does in the
    // current reorder mode.
    void append(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup,
                const Effects& effects);
    void emit_word(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup,
                   Emitted& placed);
    void emit_nops(Assembler& as, unsigned count);
    void insert_history(std::size_t first, std::size_t count, const Emitted& entry);
    // `jumped`: after an unconditional jump, which GNU as marks.
    void clear_history(bool jumped = false);
    void move_labels(Assembler& as);
    void start_noreorder(Assembler& as);
    void end_noreorder(Assembler& as);
    [[nodiscard]] int insns_between(const Effects& first, const Effects* second) const;
    [[nodiscard]] int nops_for(std::size_t ignore, const Emitted* history, const Effects* next) const;
    [[nodiscard]] int nops_for_sequence(std::size_t ignore, const Emitted* history,
                                        const Effects& older, const Effects& newer) const;
    [[nodiscard]] int nops_for_target(std::size_t ignore, const Emitted* history,
                                      const Effects& next) const;
    [[nodiscard]] bool can_swap(Assembler& as, const Effects& branch,
                                std::uint32_t word,
                                const std::optional<PendingFixup>& fixup) const;
    // -mfix-r5900: whether `word` ends a loop of six instructions or fewer.
    [[nodiscard]] bool short_loop(Assembler& as, std::uint32_t word,
                                  const std::optional<PendingFixup>& fixup) const;
    // -mfix-loongson3-llsc: the SYNC that GNU as puts before an
    // instruction.
    void fix_loongson3_llsc(Assembler& as, Sequence sequence);

    void use_gpr(unsigned number) {
        if (!model_.gnu) gpr_mask_ |= 1U << number;
    }
    void use_fpr(unsigned number, bool wide) {
        if (model_.gnu) return;
        cpr_mask_[1] |= 1U << number;
        // As LLVM MC does, a double marks both halves of its pair unless
        // the CPU has 64-bit FPRs (MIPS III or later) or the ABI uses them.
        if (wide && !model_.fr1 && !model_.isa64 && number < 31) {
            cpr_mask_[1] |= 1U << (number + 1);
        }
    }
    bool check_double(Assembler& as, unsigned number, SourceLocation location);
    // Single-precision arithmetic may name odd registers: GNU as's rule for
    // assembler input, the object's tag for compiler output.
    [[nodiscard]] bool odd_singles() const {
        if (!model_.gnu) return model_.fr1 || model_.odd_spreg;
        return (options_.architecture.odd_single || options_.fpr == FprMode::Fp64) &&
               options_.odd_spreg;
    }
    // An arithmetic operand: odd registers hold doubles only with 64-bit
    // FPRs and singles only where odd single-precision registers exist.
    bool check_operand(Assembler& as, unsigned number, bool wide, SourceLocation location);
    // `warn` reports a multi-instruction expansion under `.set nomacro`.
    void load_immediate(Assembler& as, unsigned target, std::int64_t value, bool wide,
                        SourceLocation location, bool warn = true);
    // Checks that a macro may use $at; false after a diagnostic.
    bool use_at(Assembler& as, SourceLocation location);
    // Whether the current architecture has the form.
    [[nodiscard]] bool available(const Form& form) const {
        const auto features = options_.architecture.features;
        return (form.features == 0 || (form.features & features) != 0) &&
               (form.excludes & features) == 0;
    }
    // Whether operand texts, split at commas, have the shape of the form's
    // operands: their number, and registers where it has registers.
    [[nodiscard]] bool shape_matches(const ModelInstruction& entry,
                                     const std::vector<std::string_view>& operands) const;
    // Of several forms whose shape the operand texts have, the first whose
    // immediate operands, where their values are known now, take them;
    // otherwise the first. nullopt after a diagnostic.
    std::optional<const Form*> fitting_form(Assembler& as, const Cursor& cursor,
                                            const std::vector<const Form*>& candidates,
                                            const std::vector<std::string_view>& operands);
    // Parses the form's operands; nullopt after a diagnostic.
    std::optional<std::vector<OperandValue>> parse_operands(Assembler& as, Cursor& cursor,
                                                            const ModelInstruction& entry,
                                                            std::string_view mnemonic);
    bool parse_operand(Assembler& as, Cursor& cursor, const ModelInstruction& entry,
                       std::size_t index, std::string_view mnemonic, OperandValue& value);
    // Encodes and appends an instruction of the form; false after a
    // diagnostic.
    bool emit(Assembler& as, const Form& form, const std::vector<OperandValue>& values);
    // The form of a shipped or model entry by name, as macros emit it.
    const Form& named(std::string_view name) const { return *named_.at(name); }
    // Expands an entry with `expands` or `otherwise`.
    void expand(Assembler& as, const ModelInstruction& entry, const std::vector<OperandValue>& values,
                SourceLocation location);
    [[nodiscard]] Effects effects_of(const Form& form, const std::vector<OperandValue>& values) const;
    void set_directive(Assembler& as, Statement& statement);
    void module_directive(Assembler& as, Statement& statement);
    // The architecture `mipsN` or `arch=NAME` selects; nullopt after a
    // diagnostic. `matched` is false when the option is neither.
    std::optional<Architecture> named_architecture(Assembler& as, std::string_view option,
                                                   SourceLocation location, bool& matched);
    // An option that .set and .module share, applied to `settings`.
    enum class OptionResult : std::uint8_t { Unknown, Handled, Architecture, Failed };
    OptionResult code_option(Assembler& as, std::string_view option, SourceLocation location,
                             Settings& settings);
    void end_procedure(Assembler& as, Statement& statement);
    // GNU as aligns a data directive's values and moves the labels before
    // them; the generic directive then emits the values.
    void align_data(Assembler& as, std::string_view name);
    void align_with_labels(Assembler& as, std::uint64_t alignment,
                           std::optional<std::uint8_t> fill,
                           const std::vector<SymbolId>& labels);

    Model model_;
    const Subtarget& subtarget_;
    unsigned optimize_{2};
    Settings file_;
    Settings options_;
    std::vector<Settings> stack_;
    std::unordered_map<std::string_view, std::vector<Form>> forms_;
    // Forms by entry name, and the form of each encoded entry.
    std::unordered_map<std::string_view, const Form*> named_;
    std::unordered_map<const ModelInstruction*, const Form*> encoded_forms_;
    bool noreorder_{};
    bool pic_{};
    bool nan2008_{};
    // Code or a .set directive appeared, after which .module may not.
    bool started_{};
    // .module or a command option gave these file-level settings, which an
    // ISA selected later leaves as they are.
    bool gp_explicit_{};
    bool fp_explicit_{};
    bool odd_spreg_explicit_{};
    // Data directives align their values (`.align 0` turns this off until
    // the next section change).
    bool auto_align_{true};
    std::uint32_t gpr_mask_{};
    std::array<std::uint32_t, 4> cpr_mask_{};
    unsigned emitted_{};
    std::array<Emitted, history_size> history_{};
    // Labels defined since the last instruction.
    std::vector<SymbolId> labels_;
    // The instruction being assembled names its FPR as `$N`.
    bool numeric_fpr_{};
    // A .file directive appeared.
    bool file_seen_{};
    // Nops `.set noreorder` added for hazards of earlier instructions, which
    // later instructions may not need: their fragment and counts as GNU as
    // keeps them.
    struct TentativeNops {
        assembler::SectionId section{};
        std::uint32_t fragment{};
        unsigned holds{};
        unsigned required{};
        unsigned since{};
    };
    std::optional<TentativeNops> tentative_;
    // Procedure description from .ent/.frame/.mask/.fmask for .pdr.
    struct Procedure {
        std::optional<assembler::SymbolId> symbol;
        std::uint32_t gpr_mask{};
        std::int64_t gpr_offset{};
        std::uint32_t fpr_mask{};
        std::int64_t fpr_offset{};
        std::int64_t frame_offset{};
        unsigned frame_register{};
        unsigned return_register{};
    } procedure_;
};

std::optional<unsigned> MipsTarget::gpr(Assembler& as, Cursor& cursor) {
    cursor.skip_space();
    const auto location = cursor.location();
    if (!cursor.consume("$")) {
        as.error(location, "expected a general register");
        return std::nullopt;
    }
    const auto rest = cursor.rest();
    std::size_t end = 0;
    while (end < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[end])) != 0)) ++end;
    const auto name = rest.substr(0, end);
    const auto number = gpr_number(name, model_.elf64);
    if (!number) {
        if (model_.elf64 && (name == "t4" || name == "t5" || name == "t6" || name == "t7")) {
            as.error(location, "register $" + std::string(name) +
                                   " does not exist in the n64 ABI; use $12-$15 or $t0-$t3");
        } else {
            as.error(location, "unknown general register '$" + std::string(name) + "'");
        }
        return std::nullopt;
    }
    cursor.set_position(cursor.position() + end);
    if (*number == 1 && options_.at) {
        as.warning(location, "used $at without \".set noat\"");
    }
    return number;
}

std::optional<unsigned> MipsTarget::fpr(Assembler& as, Cursor& cursor, bool numeric) {
    cursor.skip_space();
    const auto location = cursor.location();
    const auto rest = cursor.rest();
    std::size_t start = 0;
    if (rest.starts_with("$f") && !rest.starts_with("$fp") && !rest.starts_with("$fcc")) {
        start = 2;
    } else if (numeric && rest.size() > 1 && rest[0] == '$' && rest[1] >= '0' && rest[1] <= '9') {
        start = 1;
    }
    if (start != 0) {
        std::size_t end = start;
        while (end < rest.size() && rest[end] >= '0' && rest[end] <= '9') ++end;
        const bool whole = end == rest.size() ||
                           std::isalnum(static_cast<unsigned char>(rest[end])) == 0;
        if (const auto number = small_number(rest.substr(start, end - start), 31); number && whole) {
            cursor.set_position(cursor.position() + end);
            numeric_fpr_ = start == 1;
            return number;
        }
    }
    as.error(location, "expected a floating-point register");
    return std::nullopt;
}

std::optional<unsigned> MipsTarget::condition_code(Cursor& cursor) {
    cursor.skip_space();
    const auto rest = cursor.rest();
    if (!rest.starts_with("$fcc") || rest.size() < 5 || rest[4] < '0' || rest[4] > '7' ||
        (rest.size() > 5 && std::isalnum(static_cast<unsigned char>(rest[5])) != 0)) {
        return std::nullopt;
    }
    cursor.set_position(cursor.position() + 5);
    return static_cast<unsigned>(rest[4] - '0');
}

bool MipsTarget::comma(Assembler& as, Cursor& cursor) {
    if (cursor.consume(",")) return true;
    as.error(cursor.location(), "expected ','");
    return false;
}

std::optional<Immediate> MipsTarget::immediate(Assembler& as, Cursor& cursor) {
    cursor.skip_space();
    Immediate result;
    result.location = cursor.location();
    if (cursor.peek() == '%') {
        cursor.consume("%");
        const auto name = cursor.identifier();
        if (!name || !cursor.consume("(")) {
            as.error(result.location, "expected a relocation operator such as %lo(symbol)");
            return std::nullopt;
        }
        if (*name == "hi") result.relocation = fixup_hi16;
        else if (*name == "lo") result.relocation = fixup_lo16;
        else if (*name == "gp_rel") result.relocation = fixup_gprel16;
        else if (*name == "higher") result.relocation = fixup_higher;
        else if (*name == "highest") result.relocation = fixup_highest;
        else {
            as.error(result.location, "relocation operator %" + std::string(*name) +
                                          " is not supported");
            return std::nullopt;
        }
        const auto expression = as.parse_expression(cursor);
        if (!expression) return std::nullopt;
        if (!cursor.consume(")")) {
            as.error(cursor.location(), "expected ')'");
            return std::nullopt;
        }
        result.expression = *expression;
        return result;
    }
    const auto expression = as.parse_expression(cursor);
    if (!expression) return std::nullopt;
    result.expression = *expression;
    return result;
}

bool MipsTarget::sixteen(Assembler& as, const Immediate& value, std::int64_t minimum,
                         std::int64_t maximum, OperandValue& result) {
    if (value.relocation) {
        result.fixup = PendingFixup{*value.relocation, value.expression, value.location};
        return true;
    }
    if (const auto number = as.absolute_now(value.expression)) {
        if (*number < minimum || *number > maximum) {
            as.error(value.location, "immediate " + std::to_string(*number) +
                                         " is out of range [" + std::to_string(minimum) + ", " +
                                         std::to_string(maximum) + "]");
            return false;
        }
        result.value = *number;
        return true;
    }
    result.fixup = PendingFixup{minimum < 0 ? fixup_signed16 : fixup_unsigned16,
                                value.expression, value.location};
    return true;
}

std::optional<std::int64_t> MipsTarget::absolute(Assembler& as, Cursor& cursor, std::int64_t low,
                                                 std::int64_t high, std::string_view what) {
    cursor.skip_space();
    const auto location = cursor.location();
    const auto value = as.parse_absolute(cursor);
    if (!value) return std::nullopt;
    if (*value < low || *value > high) {
        as.error(location, std::string(what) + " " + std::to_string(*value) +
                               " is out of range [" + std::to_string(low) + ", " +
                               std::to_string(high) + "]");
        return std::nullopt;
    }
    return value;
}

// ---------------------------------------------------------------------------
// Reorder mode, after GNU as: hazard nops before an instruction, and a
// branch's delay slot filled with the instruction before it when they can
// trade places, else with a nop.

int MipsTarget::insns_between(const Effects& first, const Effects* second) const {
    using namespace effect;
    const auto earlier = first.flags;
    const auto later = second ? second->flags : any;
    const auto& lock = options_.architecture.interlocks;
    if (!lock.hilo) {
        if ((earlier & read_lo) != 0 && (later & write_lo) != 0) return 2;
        if ((earlier & read_hi) != 0 && (later & write_hi) != 0) return 2;
    }
    // Coprocessor 0 moves have a load's delay; FPU moves their own.
    const bool move_locked = (earlier & cp0) != 0 ? lock.load : lock.transfer;
    if ((!lock.load && (earlier & load_memory) != 0) ||
        (!move_locked && (earlier & load_coprocessor) != 0)) {
        if (!second || (second->gpr_read & first.gpr_write) != 0) return 1;
    } else if ((!move_locked && (earlier & coprocessor_move) != 0) ||
               (!lock.load && (earlier & coprocessor_load) != 0)) {
        if (first.fpr_write != 0) {
            if (!second || (first.fpr_write & second->fpr_read) != 0) return 1;
        } else {
            if ((earlier & write_condition) != 0 && (later & read_condition) != 0) return 2;
            if ((later & coprocessor) != 0) return 1;
        }
    } else if (!lock.compare && (earlier & cp0) == 0 && (earlier & write_condition) != 0 &&
               (later & read_condition) != 0) {
        return 1;
    }
    return 0;
}

int MipsTarget::nops_for(std::size_t ignore, const Emitted* history, const Effects* next) const {
    int nops = 0;
    for (std::size_t index = ignore; index < max_delay_nops; ++index) {
        nops = std::max(nops, insns_between(history[index].effects, next) - static_cast<int>(index));
    }
    return nops;
}

int MipsTarget::nops_for_sequence(std::size_t ignore, const Emitted* history,
                                  const Effects& older, const Effects& newer) const {
    std::array<Emitted, history_size> buffer{};
    buffer[0].effects = newer;
    buffer[1].effects = older;
    std::copy(history, history + history_size - 2, buffer.begin() + 2);
    return nops_for(ignore, buffer.data(), nullptr);
}

int MipsTarget::nops_for_target(std::size_t ignore, const Emitted* history,
                                const Effects& next) const {
    int nops = nops_for(ignore, history, &next);
    if ((next.flags & effect::delayed) != 0) {
        nops = std::max(nops, nops_for_sequence(ignore != 0 ? ignore + 2 : 0, history, next, {}));
    }
    return nops;
}

bool MipsTarget::can_swap(Assembler& as, const Effects& branch, std::uint32_t word,
                          const std::optional<PendingFixup>& fixup) const {
    using namespace effect;
    const auto& previous = history_[0];
    if (optimize_ < 2 || !options_.move || previous.fixed || history_[1].noreorder ||
        !labels_.empty() || !previous.placed) {
        return false;
    }
    if (subtarget_.has_feature(Feature::FixR5900) && short_loop(as, word, fixup)) return false;
    // The previous instruction must end the current fragment.
    const auto& section = as.section(as.current_section());
    if (!(previous.section == as.current_section()) ||
        previous.fragment + 1 != section.fragments.size() ||
        section.fragments.back().kind != assembler::Fragment::Kind::Data ||
        previous.offset + 4 != section.fragments.back().bytes.size()) {
        return false;
    }
    const auto& before = previous.effects;
    if ((before.flags & no_delay_slot) != 0) return false;
    if (nops_for(0, history_.data() + 1, &branch) > 0) return false;
    if (nops_for_sequence(0, history_.data() + 1, branch, before) > 0) return false;
    if ((branch.gpr_read & before.gpr_write) != 0 || (branch.fpr_read & before.fpr_write) != 0 ||
        (branch.gpr_write & before.gpr_write) != 0 || (branch.gpr_write & before.gpr_read) != 0) {
        return false;
    }
    if (((branch.flags & read_condition) != 0 && (before.flags & write_condition) != 0) ||
        ((branch.flags & write_condition) != 0 && (before.flags & read_condition) != 0)) {
        return false;
    }
    return true;
}

// GNU as's rule: a conditional branch back to a label of the same fragment
// at most 20 bytes before it, with no branch and no history an unconditional
// jump cleared in between, ends a short loop.
bool MipsTarget::short_loop(Assembler& as, std::uint32_t word,
                            const std::optional<PendingFixup>& fixup) const {
    if (!fixup || fixup->kind != fixup_branch16) return false;
    // beq $0,$0 and bgez $0 are b, bgezal $0 is bal.
    const auto high = word & 0xffff0000U;
    if (high == 0x10000000U || high == 0x04010000U || high == 0x04110000U) return false;
    const auto& target = as.expression(fixup->expression);
    if (target.kind != assembler::Expression::Kind::Symbol) return false;
    const auto& symbol = as.symbol(target.symbol);
    const auto& section = as.section(as.current_section());
    if (!symbol.section || !(*symbol.section == as.current_section()) ||
        symbol.fragment + 1 != section.fragments.size()) {
        return false;
    }
    const auto distance = static_cast<std::int64_t>(section.fragments.back().bytes.size()) -
                          static_cast<std::int64_t>(symbol.offset);
    if (distance > 20) return false;
    for (std::int64_t index = 0; index < distance / 4; ++index) {
        const auto& entry = history_[static_cast<std::size_t>(index)];
        if (entry.cleared || (entry.effects.flags & effect::delayed) != 0) return false;
    }
    return true;
}

// GNU as puts a SYNC before an LL or LLD that does not follow one, and at a
// local label, unless a SYNC is there, that a branch between an LL and a
// later SC within the last five instructions targets.
void MipsTarget::fix_loongson3_llsc(Assembler& as, Sequence sequence) {
    const auto sync = [&] {
        const auto& form = named("sync");
        const auto word = static_cast<std::uint32_t>(form.entry->fixed_bits);
        Emitted entry;
        entry.effects = effects_of(form, {});
        entry.sequence = Sequence::Sync;
        emit_word(as, word, {}, entry);
        insert_history(0, 1, entry);
    };
    const auto exit_target = [&] {
        if (labels_.empty() || !as.symbol(labels_.back()).temporary ||
            sequence == Sequence::Sync) {
            return false;
        }
        for (std::size_t store = 0; store < history_size; ++store) {
            if (history_[store].sequence != Sequence::StoreConditional) continue;
            for (std::size_t index = store + 1; index < history_size; ++index) {
                const auto& entry = history_[index];
                if (entry.sequence == Sequence::LoadLinked) break;
                if ((entry.effects.flags & effect::delayed) != 0 && entry.target &&
                    std::find(labels_.begin(), labels_.end(), *entry.target) != labels_.end()) {
                    return true;
                }
            }
        }
        return false;
    };
    if (exit_target()) sync();
    if (sequence == Sequence::LoadLinked && history_[0].sequence != Sequence::Sync) sync();
}

void MipsTarget::insert_history(std::size_t first, std::size_t count, const Emitted& entry) {
    for (std::size_t index = history_size; index-- > first;) {
        history_[index] = index >= first + count ? history_[index - count] : entry;
    }
}

void MipsTarget::clear_history(bool jumped) {
    tentative_.reset();
    Emitted placeholder;
    placeholder.cleared = jumped;
    history_.fill(placeholder);
    labels_.clear();
}

void MipsTarget::move_labels(Assembler& as) {
    for (const auto label : labels_) as.move_label(label);
}

void MipsTarget::emit_nops(Assembler& as, unsigned count) {
    static constexpr std::array<std::uint8_t, 4> nop{};
    for (unsigned index = 0; index < count; ++index) as.emit_padding(nop);
}

void MipsTarget::emit_word(Assembler& as, std::uint32_t word,
                           const std::optional<PendingFixup>& fixup, Emitted& placed) {
    if (fixup) as.add_fixup(FixupKind{fixup->kind}, fixup->expression, fixup->location);
    as.emit_integer(word, 4);
    const auto& section = as.section(as.current_section());
    placed.placed = true;
    placed.section = as.current_section();
    placed.fragment = static_cast<std::uint32_t>(section.fragments.size() - 1);
    placed.offset = section.fragments.back().bytes.size() - 4;
    placed.has_fixup = fixup.has_value();
    ++emitted_;
}

void MipsTarget::append(Assembler& as, std::uint32_t word,
                        const std::optional<PendingFixup>& fixup, const Effects& effects) {
    using namespace effect;
    started_ = true;
    if (model_.gnu) {
        gpr_mask_ |= effects.gpr_read | effects.gpr_write;
        cpr_mask_[1] |= effects.fpr_read | effects.fpr_write;
    }
    const unsigned major = word >> 26U;
    const auto sequence = major == 0x30 || major == 0x34   ? Sequence::LoadLinked
                          : major == 0x38 || major == 0x3c ? Sequence::StoreConditional
                          : (word & 0xfc00003fU) == 0x0000000fU ? Sequence::Sync
                                                                : Sequence::None;
    // GNU as applies the fix in either reorder mode.
    if (subtarget_.has_feature(Feature::FixLoongson3Llsc)) fix_loongson3_llsc(as, sequence);
    if (!options_.noreorder) {
        const int nops = optimize_ == 0 ? nops_for(0, history_.data(), nullptr)
                                        : nops_for_target(0, history_.data(), effects);
        if (nops > 0) {
            emit_nops(as, static_cast<unsigned>(nops));
            insert_history(0, static_cast<std::size_t>(nops), Emitted{});
            move_labels(as);
        }
    } else if (tentative_) {
        // An instruction after `.set noreorder` decides how many of its
        // tentative nops the earlier hazards need.
        const int nops = nops_for_target(tentative_->since, history_.data(), effects);
        tentative_->required = std::max(tentative_->required, static_cast<unsigned>(nops));
        if (tentative_->holds == tentative_->required) {
            insert_history(tentative_->since, tentative_->holds, Emitted{});
            tentative_.reset();
        } else {
            auto& bytes = as.section(tentative_->section).fragments[tentative_->fragment].bytes;
            bytes.resize(bytes.size() - 4);
            --tentative_->holds;
            ++tentative_->since;
        }
    }
    Emitted entry;
    entry.effects = effects;
    entry.fixed = options_.noreorder;
    entry.noreorder = options_.noreorder;
    entry.sequence = sequence;
    if (fixup && fixup->kind == fixup_branch16) {
        const auto& target = as.expression(fixup->expression);
        if (target.kind == assembler::Expression::Kind::Symbol) entry.target = target.symbol;
    }
    const bool branch = (effects.flags & delayed) != 0;
    if (!options_.noreorder && branch && (effects.flags & likely) == 0 &&
        can_swap(as, effects, word, fixup)) {
        // The previous instruction moves into the delay slot, with its fixup.
        auto delay = history_[0];
        auto& fragment = as.section(as.current_section()).fragments.back();
        std::optional<assembler::Fixup> moved;
        if (delay.has_fixup) {
            moved = fragment.fixups.back();
            fragment.fixups.pop_back();
        }
        const auto previous_word = static_cast<std::uint32_t>(
            as.read_integer(std::span<const std::uint8_t>(fragment.bytes).subspan(delay.offset, 4)));
        fragment.bytes.resize(delay.offset);
        emit_word(as, word, fixup, entry);
        if (moved) {
            // The fixups stay in the order they were created.
            moved->offset = static_cast<std::uint32_t>(fragment.bytes.size());
            fragment.fixups.insert(fragment.fixups.end() - (fixup ? 1 : 0), *moved);
        }
        as.emit_padding(std::array<std::uint8_t, 4>{});
        as.write_integer(std::span<std::uint8_t>(fragment.bytes).subspan(fragment.bytes.size() - 4, 4),
                         previous_word);
        delay.offset = fragment.bytes.size() - 4;
        delay.fixed = true;
        history_[0] = entry;
        insert_history(0, 1, delay);
    } else {
        emit_word(as, word, fixup, entry);
        insert_history(0, 1, entry);
        if (!options_.noreorder && branch) {
            emit_nops(as, 1);
            insert_history(0, 1, Emitted{});
        }
    }
    // After an unconditional jump and its delay slot nothing earlier
    // reaches the next instruction.
    if ((history_[1].effects.flags & delayed) != 0 &&
        (history_[1].effects.flags & effect::unconditional) != 0) {
        clear_history(true);
    }
    labels_.clear();
}

void MipsTarget::flush(Assembler& as) {
    if (!options_.noreorder) {
        if (const int nops = nops_for(0, history_.data(), nullptr); nops > 0) {
            emit_nops(as, static_cast<unsigned>(nops));
            move_labels(as);
        }
    }
    clear_history();
}

void MipsTarget::start_noreorder(Assembler& as) {
    for (auto& entry : history_) entry.fixed = true;
    const int nops = nops_for(0, history_.data(), nullptr);
    if (nops > 0) {
        auto& section = as.section(as.current_section());
        // The nops get a fragment of their own so later instructions can
        // drop the ones they do not need.
        if (optimize_ != 0) {
            assembler::Fragment fragment;
            fragment.subsection = section.fragments.back().subsection;
            section.fragments.push_back(std::move(fragment));
            tentative_ = TentativeNops{as.current_section(),
                                       static_cast<std::uint32_t>(section.fragments.size() - 1),
                                       static_cast<unsigned>(nops), 0, 0};
        }
        emit_nops(as, static_cast<unsigned>(nops));
        if (optimize_ != 0) {
            assembler::Fragment fragment;
            fragment.subsection = section.fragments.back().subsection;
            section.fragments.push_back(std::move(fragment));
        }
        move_labels(as);
    }
    labels_.clear();
}

void MipsTarget::end_noreorder(Assembler& as) {
    if (!tentative_) return;
    // Settle on the nops the instructions so far needed.
    auto& bytes = as.section(tentative_->section).fragments[tentative_->fragment].bytes;
    bytes.resize(bytes.size() - std::size_t{tentative_->holds - tentative_->required} * 4);
    insert_history(tentative_->since, tentative_->required, Emitted{});
    tentative_.reset();
}

bool MipsTarget::check_operand(Assembler& as, unsigned number, bool wide,
                               SourceLocation location) {
    if (wide) return check_double(as, number, location);
    if (odd_singles() || (number & 1U) == 0) return true;
    as.error(location, "a single-precision operation needs an even register without odd "
                       "single-precision registers, not $f" + std::to_string(number));
    return false;
}

bool MipsTarget::check_double(Assembler& as, unsigned number, SourceLocation location) {
    if ((model_.gnu ? options_.fpr == FprMode::Fp64 : model_.fr1) || (number & 1U) == 0) return true;
    as.error(location, "a double-precision value needs an even register with 32-bit FPRs, not $f" +
                           std::to_string(number));
    return false;
}

bool MipsTarget::use_at(Assembler& as, SourceLocation location) {
    if (!options_.at) {
        as.error(location, "this macro needs $at, which \".set noat\" reserves");
        return false;
    }
    if (!options_.macro) {
        as.warning(location, "macro instruction expanded into multiple instructions");
    }
    return true;
}

void MipsTarget::load_immediate(Assembler& as, unsigned target, std::int64_t value, bool wide,
                                SourceLocation location, bool warn) {
    // LLVM MC's expansion of li and dli, which GNU as matches for every
    // 32-bit value, with the table's instructions.
    const auto start = emitted_;
    const auto t = static_cast<std::int64_t>(target);
    const auto put = [&](std::string_view name, std::initializer_list<std::int64_t> operands) {
        std::vector<OperandValue> values;
        for (const auto operand : operands) {
            OperandValue item;
            item.value = operand;
            item.location = location;
            values.push_back(item);
        }
        (void)emit(as, named(name), values);
    };
    const auto addiu = [&](std::int64_t immediate) {
        put("addiu", {t, 0, static_cast<std::int16_t>(immediate & 0xffff)});
    };
    const auto ori_zero = [&](std::uint64_t immediate) {
        put("ori", {t, 0, static_cast<std::int64_t>(immediate & 0xffffU)});
    };
    const auto ori = [&](std::uint64_t immediate) {
        put("ori", {t, t, static_cast<std::int64_t>(immediate & 0xffffU)});
    };
    const auto lui = [&](std::uint64_t immediate) {
        put("lui", {t, static_cast<std::int64_t>(immediate & 0xffffU)});
    };
    const auto dsll = [&](unsigned amount) { put("dsll", {t, t, amount}); };
    const std::function<void(std::int64_t, bool)> load = [&](std::int64_t number, bool word) {
        if (word) number = static_cast<std::int32_t>(static_cast<std::uint32_t>(number));
        const auto unsigned_number = static_cast<std::uint64_t>(number);
        if (is_int16(number)) {
            addiu(number);
            return;
        }
        if (is_uint16(number)) {
            ori_zero(unsigned_number);
            return;
        }
        const bool is_int32 = number >= std::numeric_limits<std::int32_t>::min() &&
                              number <= std::numeric_limits<std::int32_t>::max();
        if (is_int32 || unsigned_number <= 0xffffffffU) {
            const auto high = (unsigned_number >> 16U) & 0xffffU;
            const auto low = unsigned_number & 0xffffU;
            if (!word && !is_int32) {
                if (unsigned_number == 0xffffffffU) {
                    lui(0xffff);
                    put("dsrl32", {t, t, 0});
                    return;
                }
                ori_zero(high);
                dsll(16);
                if (low != 0) ori(low);
                return;
            }
            lui(high);
            if (low != 0) ori(low);
            return;
        }
        const auto width = static_cast<unsigned>(std::bit_width(unsigned_number));
        const auto trailing = width - 16;
        if (((unsigned_number >> trailing) << trailing) == unsigned_number &&
            static_cast<unsigned>(std::countr_zero(unsigned_number)) >= trailing) {
            ori_zero((unsigned_number >> trailing) & 0xffffU);
            dsll(trailing);
            return;
        }
        load(static_cast<std::int64_t>(unsigned_number >> 32U), true);
        unsigned carried = 16;
        for (int bit = 16; bit >= 0; bit -= 16) {
            const auto chunk = (unsigned_number >> static_cast<unsigned>(bit)) & 0xffffU;
            if (chunk != 0) {
                dsll(carried);
                ori(chunk);
                carried = 0;
            }
            carried += 16;
        }
        carried -= 16;
        if (carried != 0) dsll(carried);
    };
    if (!wide) {
        const auto unsigned_value = static_cast<std::uint64_t>(value);
        if (!(value >= std::numeric_limits<std::int32_t>::min() &&
              value <= std::numeric_limits<std::int32_t>::max()) &&
            unsigned_value > 0xffffffffU) {
            as.error(location, "li needs a 32-bit immediate; use dli for 64-bit values");
            return;
        }
    }
    load(value, !wide);
    if (warn && !options_.macro && emitted_ - start > 1) {
        as.warning(location, "macro instruction expanded into multiple instructions");
    }
}

// The operand texts of a statement, split at the commas outside
// parentheses and quoted names.
std::vector<std::string_view> split_operands(std::string_view text) {
    const auto trim = [](std::string_view item) {
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t' || item.back() == '\r')) {
            item.remove_suffix(1);
        }
        return item;
    };
    std::vector<std::string_view> result;
    text = trim(text);
    if (text.empty()) return result;
    int depth = 0;
    bool quoted = false;
    std::size_t start = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '"') quoted = !quoted;
        else if (quoted) continue;
        else if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) {
            result.push_back(trim(text.substr(start, index - start)));
            start = index + 1;
        }
    }
    result.push_back(trim(text.substr(start)));
    return result;
}

bool MipsTarget::shape_matches(const ModelInstruction& entry,
                               const std::vector<std::string_view>& operands) const {
    std::size_t required = 0;
    for (const auto& operand : entry.operands) {
        if (!operand.default_value && !operand.default_operand) ++required;
    }
    if (operands.size() < required || operands.size() > entry.operands.size()) return false;
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const auto& operand = entry.operands[index];
        const auto text = operands[index];
        if (operand.name.empty()) {
            const auto number = text.starts_with('$') ? gpr_number(text.substr(1), model_.elf64)
                                                      : std::nullopt;
            if (!number || *number != operand.literal_number) return false;
            continue;
        }
        switch (type_id(operand)) {
        case OperandTypeId::Immediate:
        case OperandTypeId::Expression:
            if (text.starts_with('$')) return false;
            break;
        case OperandTypeId::Memory:
        case OperandTypeId::Branch:
        case OperandTypeId::Jump:
            break;
        default:
            if (!text.starts_with('$')) return false;
            break;
        }
    }
    return true;
}

std::optional<const Form*> MipsTarget::fitting_form(Assembler& as, const Cursor& cursor,
                                                    const std::vector<const Form*>& candidates,
                                                    const std::vector<std::string_view>& operands) {
    // The values of the operands that every candidate takes as immediates
    // without relocations, where they are known now.
    const auto rest = cursor.rest();
    std::vector<std::optional<std::int64_t>> values(operands.size());
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const bool immediate =
            std::all_of(candidates.begin(), candidates.end(), [&](const Form* form) {
                const auto& operand = form->entry->operands[index];
                return !operand.name.empty() && type_id(operand) == OperandTypeId::Immediate &&
                       operand.type.relocated_bits == 0;
            });
        if (!immediate) continue;
        auto probe = cursor;
        probe.set_position(cursor.position() +
                           static_cast<std::size_t>(operands[index].data() - rest.data()));
        const auto expression = as.parse_expression(probe);
        if (!expression) return std::nullopt;
        values[index] = as.absolute_now(*expression);
    }
    const auto accepts = [&](const ModelInstruction& entry) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto& type = entry.operands[index].type;
            if (values[index] && (*values[index] < type.minimum || *values[index] > type.maximum)) {
                return false;
            }
        }
        for (const auto& constraint : entry.constraints) {
            std::int64_t sum = constraint.constant;
            bool known = true;
            for (const auto& term : constraint.terms) {
                if (term.operand >= values.size() || !values[term.operand]) {
                    known = false;
                    break;
                }
                sum += term.negative ? -*values[term.operand] : *values[term.operand];
            }
            if (known && !constraint.holds(sum)) return false;
        }
        return true;
    };
    for (const auto* form : candidates) {
        if (accepts(*form->entry)) return form;
    }
    return candidates.front();
}

bool MipsTarget::parse_operand(Assembler& as, Cursor& cursor, const ModelInstruction& entry,
                               std::size_t index, std::string_view mnemonic,
                               OperandValue& value) {
    const auto& operand = entry.operands[index];
    cursor.skip_space();
    value.location = cursor.location();
    if (operand.name.empty()) {
        const auto number = gpr(as, cursor);
        if (!number) return false;
        if (*number != operand.literal_number) {
            as.error(value.location, "'" + std::string(mnemonic) + "' takes '" +
                                         operand.spelling + "' as operand " +
                                         std::to_string(index + 1));
            return false;
        }
        value.value = *number;
        return true;
    }
    const auto id = type_id(operand);
    switch (id) {
    case OperandTypeId::Gpr: {
        const auto number = gpr(as, cursor);
        if (!number) return false;
        value.value = *number;
        return true;
    }
    case OperandTypeId::Fpr32:
    case OperandTypeId::Fpr64: {
        const auto number = fpr(as, cursor);
        if (!number || !check_operand(as, *number, id == OperandTypeId::Fpr64, value.location)) {
            return false;
        }
        value.value = *number;
        return true;
    }
    case OperandTypeId::Cpr:
    case OperandTypeId::Cpr64: {
        const auto number = fpr(as, cursor, true);
        if (!number) return false;
        value.numeric = numeric_fpr_;
        numeric_fpr_ = false;
        if (!value.numeric && id == OperandTypeId::Cpr64 &&
            !check_double(as, *number, value.location)) {
            return false;
        }
        value.value = *number;
        return true;
    }
    case OperandTypeId::Fcc: {
        const auto cc = condition_code(cursor);
        if (!cc) {
            as.error(cursor.location(), "expected a condition code register $fcc0-$fcc7");
            return false;
        }
        value.value = *cc;
        return true;
    }
    case OperandTypeId::Cp0: {
        const auto rest = cursor.rest();
        std::optional<unsigned> number;
        std::size_t end = 1;
        if (rest.starts_with("$")) {
            while (end < rest.size() && rest[end] >= '0' && rest[end] <= '9') ++end;
            number = small_number(rest.substr(1, end - 1), 31);
        }
        if (!number) {
            as.error(value.location, "expected a coprocessor 0 register $0-$31");
            return false;
        }
        cursor.set_position(cursor.position() + end);
        value.value = *number;
        return true;
    }
    case OperandTypeId::Fcr: {
        const auto rest = cursor.rest();
        std::optional<unsigned> control;
        std::size_t length = 0;
        if (rest.starts_with("$")) {
            auto digits = rest.substr(rest.starts_with("$f") ? 2 : 1);
            std::size_t end = 0;
            while (end < digits.size() && digits[end] >= '0' && digits[end] <= '9') ++end;
            control = small_number(digits.substr(0, end), 31);
            length = rest.size() - digits.size() + end;
        }
        if (!control) {
            as.error(value.location, "expected a floating-point control register");
            return false;
        }
        cursor.set_position(cursor.position() + length);
        value.value = *control;
        return true;
    }
    case OperandTypeId::Immediate: {
        if (operand.type.relocated_bits != 0) {
            const auto parsed = immediate(as, cursor);
            return parsed && sixteen(as, *parsed, operand.type.minimum, operand.type.maximum, value);
        }
        const auto parsed =
            absolute(as, cursor, operand.type.minimum, operand.type.maximum, "immediate");
        if (!parsed) return false;
        value.value = *parsed;
        return true;
    }
    case OperandTypeId::Expression: {
        // A macro's value: an expansion needs it now, li may leave it to
        // layout.
        if (entry.macro.empty()) {
            const auto parsed = as.parse_absolute(cursor);
            if (!parsed) return false;
            value.value = *parsed;
            return true;
        }
        const auto parsed = as.parse_expression(cursor);
        if (!parsed) return false;
        value.expression = *parsed;
        return true;
    }
    case OperandTypeId::Memory: {
        if (cursor.rest().starts_with("($")) {
            cursor.consume("(");
        } else {
            const auto offset = immediate(as, cursor);
            if (!offset || !sixteen(as, *offset, operand.type.offset_minimum,
                                    operand.type.offset_maximum, value)) {
                return false;
            }
            if (!cursor.consume("(")) {
                // Without a base, $zero: the offset is the address.
                if (cursor.at_end() && value.fixup) {
                    as.error(offset->location, "a memory operand with a symbol needs a base register");
                    return false;
                }
                return true;
            }
        }
        const auto base = gpr(as, cursor);
        if (!base) return false;
        if (!cursor.consume(")")) {
            as.error(cursor.location(), "expected ')'");
            return false;
        }
        value.base = *base;
        return true;
    }
    case OperandTypeId::Branch:
    case OperandTypeId::Jump: {
        const auto target = as.parse_expression(cursor);
        if (!target) return false;
        value.fixup = PendingFixup{id == OperandTypeId::Jump ? fixup_jump26 : fixup_branch16,
                                   *target, value.location};
        return true;
    }
    }
    return false;
}

std::optional<std::vector<OperandValue>> MipsTarget::parse_operands(
    Assembler& as, Cursor& cursor, const ModelInstruction& entry, std::string_view mnemonic) {
    std::vector<OperandValue> values(entry.operands.size());
    for (std::size_t index = 0; index < entry.operands.size(); ++index) {
        const auto& operand = entry.operands[index];
        if (operand.default_value || operand.default_operand) {
            const bool present = index == 0 ? !cursor.at_end() : cursor.consume(",");
            if (!present) {
                for (auto rest = index; rest < entry.operands.size(); ++rest) {
                    const auto& omitted = entry.operands[rest];
                    if (omitted.default_operand) {
                        values[rest] = values[*omitted.default_operand];
                    } else {
                        values[rest].value = omitted.default_value.value_or(0);
                    }
                }
                break;
            }
        } else if (index != 0 && !comma(as, cursor)) {
            return std::nullopt;
        }
        if (!parse_operand(as, cursor, entry, index, mnemonic, values[index])) return std::nullopt;
    }
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after operands");
        return std::nullopt;
    }
    return values;
}

Effects MipsTarget::effects_of(const Form& form, const std::vector<OperandValue>& values) const {
    const auto& plan = form.effects;
    const auto gpr_bit = [](std::int64_t number) {
        return (1U << (static_cast<unsigned>(number) & 31U)) & ~1U;
    };
    Effects result;
    for (const auto index : plan.gpr_reads) result.gpr_read |= gpr_bit(values[index].value);
    for (const auto index : plan.bases) result.gpr_read |= gpr_bit(values[index].base);
    for (const auto index : plan.gpr_writes) result.gpr_write |= gpr_bit(values[index].value);
    result.gpr_read |= plan.fixed_gpr_reads;
    result.gpr_write |= plan.fixed_gpr_writes;
    // An FPR written `$N` counts in no FPR mask, hazard, or pairing rule.
    if (std::none_of(values.begin(), values.end(),
                     [](const OperandValue& value) { return value.numeric; })) {
        for (const auto index : plan.fpr_reads) {
            result.fpr_read |= 1U << (static_cast<unsigned>(values[index].value) & 31U);
        }
        for (const auto index : plan.fpr_writes) {
            result.fpr_write |= 1U << (static_cast<unsigned>(values[index].value) & 31U);
        }
        if (plan.wide && options_.fpr != FprMode::Fp64) {
            result.fpr_read |= result.fpr_read << 1U;
            result.fpr_write |= result.fpr_write << 1U;
        }
    }
    result.flags = plan.flags;
    return result;
}

bool MipsTarget::emit(Assembler& as, const Form& form, const std::vector<OperandValue>& values) {
    const auto& entry = *form.entry;
    auto word = static_cast<std::uint32_t>(entry.fixed_bits);
    std::optional<PendingFixup> fixup;
    for (const auto& field : entry.fields) {
        std::int64_t value = field.register_number ? *field.register_number : field.constant;
        for (const auto& term : field.terms) {
            const auto& operand = values[term.operand];
            if (operand.fixup && term.part != 1) fixup = operand.fixup;
            const auto part = term.part == 1 ? operand.base : operand.value;
            value += term.negative ? -part : part;
        }
        if (field.terms.size() > 1 &&
            (value < 0 || value >= (std::int64_t{1} << std::min(field.width, 62U)))) {
            as.error(values[field.terms.back().operand].location,
                     "the operands of '" + entry.mnemonic + "' give the value " +
                         std::to_string(value) + ", which its " + std::to_string(field.width) +
                         "-bit field cannot hold");
            return false;
        }
        const auto mask = field.width >= 32 ? 0xffffffffU : (1U << field.width) - 1U;
        word |= (static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> field.low) & mask)
                << field.shift;
    }
    // Register masks for the object: every register the word names.
    for (const auto& field : entry.fields) {
        if (field.register_number) {
            if (field.register_class == "integer") use_gpr(*field.register_number);
            continue;
        }
        if (field.terms.size() != 1) continue;
        const auto& term = field.terms.front();
        const auto& operand = values[term.operand];
        switch (type_id(entry.operands[term.operand])) {
        case OperandTypeId::Gpr: use_gpr(static_cast<unsigned>(operand.value)); break;
        case OperandTypeId::Memory:
            if (term.part == 1) use_gpr(static_cast<unsigned>(operand.base));
            break;
        case OperandTypeId::Fpr32:
        case OperandTypeId::Cpr:
            if (!operand.numeric) use_fpr(static_cast<unsigned>(operand.value), false);
            break;
        case OperandTypeId::Fpr64:
        case OperandTypeId::Cpr64:
            if (!operand.numeric) use_fpr(static_cast<unsigned>(operand.value), true);
            break;
        case OperandTypeId::Cp0:
            if (!model_.gnu) cpr_mask_[0] |= 1U << (static_cast<unsigned>(operand.value) & 31U);
            break;
        default: break;
        }
    }
    append(as, word, fixup, effects_of(form, values));
    return true;
}

void MipsTarget::expand(Assembler& as, const ModelInstruction& entry,
                        const std::vector<OperandValue>& values, SourceLocation location) {
    const auto& table = *model_registry().instruction_table(subtarget_.target().architecture);
    using Kind = ModelExpansion::Operand::Kind;
    const auto bind = [&](const ModelExpansion& line) {
        std::vector<OperandValue> result;
        for (const auto& operand : line.operands) {
            OperandValue item;
            if (operand.kind == Kind::Register) {
                item.value = operand.register_number;
                item.location = location;
            } else {
                item = values[operand.operand];
                if (operand.kind == Kind::Negated) item.value = -item.value;
            }
            result.push_back(item);
        }
        // Optional operands that the line leaves out take their defaults.
        const auto& target = table.entries[line.entry];
        for (auto index = result.size(); index < target.operands.size(); ++index) {
            const auto& omitted = target.operands[index];
            OperandValue item;
            item.location = location;
            if (omitted.default_operand) item = result[*omitted.default_operand];
            else item.value = omitted.default_value.value_or(0);
            result.push_back(item);
        }
        return result;
    };
    // An immediate that its instruction cannot hold.
    const auto misfit = [&](const ModelExpansion& line) -> std::optional<std::size_t> {
        const auto& target = table.entries[line.entry];
        const auto bound = bind(line);
        for (std::size_t index = 0; index < line.operands.size(); ++index) {
            const auto& type = target.operands[index].type;
            if (line.operands[index].kind != Kind::Register && type.kind == OperandKind::Immediate &&
                (bound[index].value < type.minimum || bound[index].value > type.maximum)) {
                return index;
            }
        }
        return std::nullopt;
    };
    const auto emit_lines = [&](const std::vector<ModelExpansion>& lines) {
        for (const auto& line : lines) {
            const auto& target = table.entries[line.entry];
            const auto* form = encoded_forms_.at(&target);
            if (!available(*form)) {
                as.error(location, "macro '" + entry.mnemonic + "' needs instruction '" +
                                       target.mnemonic + "', which " +
                                       std::string(isa_level(options_.architecture.isa).name) +
                                       " does not have");
                return false;
            }
            if (!emit(as, *form, bind(line))) return false;
        }
        return true;
    };
    const auto start = emitted_;
    if (!entry.expands.empty() &&
        std::none_of(entry.expands.begin(), entry.expands.end(),
                     [&](const ModelExpansion& line) { return misfit(line).has_value(); })) {
        if (emit_lines(entry.expands) && !options_.macro && emitted_ - start > 1) {
            as.warning(location, "macro instruction expanded into multiple instructions");
        }
        return;
    }
    if (entry.otherwise.empty()) {
        for (const auto& line : entry.expands) {
            if (const auto index = misfit(line)) {
                const auto& type = table.entries[line.entry].operands[*index].type;
                as.error(values[line.operands[*index].operand].location,
                         "immediate " + std::to_string(bind(line)[*index].value) +
                             " is out of range [" + std::to_string(type.minimum) + ", " +
                             std::to_string(type.maximum) + "]");
                return;
            }
        }
        return;
    }
    // Otherwise the value goes through $at, as wide as the operand that $at
    // takes.
    if (!use_at(as, location)) return;
    const auto value = std::find_if(entry.operands.begin(), entry.operands.end(),
                                    [](const ModelOperand& operand) {
                                        return operand.type.kind == OperandKind::Expression;
                                    });
    const auto& operand_value = values[static_cast<std::size_t>(value - entry.operands.begin())];
    const bool wide = [&] {
        for (const auto& line : entry.otherwise) {
            for (std::size_t index = 0; index < line.operands.size(); ++index) {
                const auto& operand = line.operands[index];
                if (operand.kind != Kind::Register || operand.register_number != 1) continue;
                const auto& widths = table.entries[line.entry].operands[index].type.widths;
                return widths.size() > 1 ? options_.gp64
                                         : !widths.empty() && widths.front().bits == 64;
            }
        }
        return false;
    }();
    load_immediate(as, 1, operand_value.value, wide, operand_value.location, false);
    (void)emit_lines(entry.otherwise);
}

void MipsTarget::instruction(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    const auto location = statement.location;
    numeric_fpr_ = false;
    const auto found = forms_.find(name);
    if (found == forms_.end()) {
        as.error(location, "unknown instruction '" + std::string(statement.name) + "'");
        return;
    }
    const auto& forms = found->second;
    // The first form the current ISA has whose operands the text has the
    // shape of and whose immediates take their values; without one, the
    // first form it has reports why the operands do not fit.
    const auto operands = split_operands(cursor.rest());
    std::vector<const Form*> candidates;
    const Form* unavailable = nullptr;
    for (const auto& form : forms) {
        if (!shape_matches(*form.entry, operands)) continue;
        if (available(form)) {
            candidates.push_back(&form);
        } else if (!unavailable) {
            unavailable = &form;
        }
    }
    const Form* chosen = candidates.empty() ? nullptr : candidates.front();
    if (candidates.size() > 1) {
        const auto fitting = fitting_form(as, cursor, candidates, operands);
        if (!fitting) return;
        chosen = *fitting;
    }
    if (!chosen && !unavailable) {
        for (const auto& form : forms) {
            if (available(form)) {
                chosen = &form;
                break;
            }
        }
    }
    if (!chosen) {
        const bool other = std::any_of(forms.begin(), forms.end(),
                                       [&](const Form& form) { return available(form); });
        as.error(location, "instruction '" + name + "'" + (other ? " with these operands" : "") +
                               " is not available in " +
                               std::string(isa_level(options_.architecture.isa).name));
        return;
    }
    // With 32-bit GPRs GNU as expands ld and sd into word pairs.
    if (!options_.gp64 && (name == "ld" || name == "sd")) {
        as.error(location, "'" + name + "' with 32-bit GPRs is a two-word macro that is not "
                               "supported; select 64-bit GPRs with \".set gp=64\" or "
                               "\".set mips3\"");
        return;
    }
    const auto& entry = *chosen->entry;
    const auto values = parse_operands(as, cursor, entry, name);
    if (!values) return;
    if (!entry.macro.empty()) {
        // li and dli.
        const bool wide = entry.macro == "dli";
        const auto target = static_cast<unsigned>((*values)[0].value);
        const auto& value = (*values)[1];
        if (const auto number = as.absolute_now(value.expression)) {
            load_immediate(as, target, *number, wide, value.location);
            return;
        }
        if (wide) {
            (void)as.parse_absolute(cursor);
            as.error(value.location, "dli needs a value known before layout");
            return;
        }
        // A value only layout settles, such as a label difference: one addiu,
        // as GNU as assumes, whose immediate must then fit.
        std::vector<OperandValue> addiu(3);
        addiu[0].value = target;
        addiu[2].fixup = PendingFixup{fixup_signed16, value.expression, value.location};
        (void)emit(as, named("addiu"), addiu);
        return;
    }
    if (!entry.expands.empty() || !entry.otherwise.empty()) {
        expand(as, entry, *values, location);
        return;
    }
    for (const auto& constraint : entry.constraints) {
        std::int64_t sum = constraint.constant;
        for (const auto& term : constraint.terms) {
            const auto part = (*values)[term.operand].value;
            sum += term.negative ? -part : part;
        }
        if (!constraint.holds(sum)) {
            as.error((*values)[constraint.terms.back().operand].location,
                     "the operands of '" + name + "' must satisfy " + constraint.text);
            return;
        }
    }
    (void)emit(as, *chosen, *values);
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<Architecture> MipsTarget::named_architecture(Assembler& as, std::string_view option,
                                                           SourceLocation location, bool& matched) {
    const auto level = std::find_if(isa_levels.begin(), isa_levels.end(),
                                    [&](const IsaLevel& entry) { return entry.name == option; });
    std::string_view name;
    if (option.starts_with("arch=")) {
        name = option.substr(5);
    } else if (level != isa_levels.end()) {
        name = option;
    } else {
        matched = false;
        return std::nullopt;
    }
    matched = true;
    // For assembler input, `mipsN` is the model's CPU of that name with
    // exactly that level, as GNU as selects the ISA's generic CPU; the
    // compiler's `.set mipsN` changes only the level of its CPU.
    const auto& table = *subtarget_.target().subtargets;
    if (!model_.gnu && level != isa_levels.end()) {
        return with_level(table, options_.architecture, level->name, true);
    }
    if (auto cpu = cpu_architecture(subtarget_, name)) {
        if (level != isa_levels.end()) return with_level(table, *cpu, level->name, false);
        return cpu;
    }
    if (level != isa_levels.end()) {
        return with_level(table, options_.architecture, level->name, false);
    }
    as.error(location, "unknown CPU '" + std::string(name) + "'");
    return std::nullopt;
}

MipsTarget::OptionResult MipsTarget::code_option(Assembler& as, std::string_view option,
                                                 SourceLocation location, Settings& settings) {
    if (option.starts_with("at=")) {
        Cursor cursor(option.substr(3), location, false);
        const auto number = gpr(as, cursor);
        if (!number) return OptionResult::Failed;
        if (*number != 1) {
            as.error(location, "only $1 can be the assembler temporary");
            return OptionResult::Failed;
        }
        settings.at = true;
    } else if (option == "at") {
        settings.at = true;
    } else if (option == "noat") {
        settings.at = false;
    } else if (option == "move" || option == "novolatile") {
        settings.move = true;
    } else if (option == "nomove" || option == "volatile") {
        settings.move = false;
    } else if (option == "gp=32" || option == "gp=64") {
        settings.gp64 = option == "gp=64";
    } else if (option == "fp=32" || option == "fp=xx" || option == "fp=64") {
        settings.fpr = option == "fp=32" ? FprMode::Fp32 : option == "fp=xx" ? FprMode::Fpxx
                                                                             : FprMode::Fp64;
    } else if (option == "oddspreg" || option == "nooddspreg") {
        settings.odd_spreg = option == "oddspreg";
    } else if (option == "softfloat" || option == "hardfloat" || option == "singlefloat" ||
               option == "doublefloat" || option == "bopt" || option == "nobopt" ||
               option == "nomips16" || option == "nomicromips" || option == "autoextend" ||
               option == "noautoextend" || option == "insn32" || option == "noinsn32" ||
               option == "sym32" || option == "nosym32") {
        // Cross never enters a compressed mode; the others select checks
        // or macro forms Cross does not implement and leave encodings as
        // they are.
    } else {
        bool matched = false;
        const auto architecture = named_architecture(as, option, location, matched);
        if (!matched) return OptionResult::Unknown;
        if (!architecture) return OptionResult::Failed;
        settings.architecture = *architecture;
        return OptionResult::Architecture;
    }
    return OptionResult::Handled;
}

void MipsTarget::set_directive(Assembler& as, Statement& statement) {
    auto& cursor = statement.operands;
    {
        // `.set NAME, VALUE` assigns a symbol.
        auto probe = cursor;
        if (probe.symbol_name()) {
            probe.skip_space();
            if (probe.peek() == ',') {
                as.assignment_statement(statement);
                return;
            }
        }
    }
    started_ = true;
    const auto option = lower(trimmed(cursor.rest()));
    const auto location = statement.location;
    if (option.empty()) {
        as.error(location, "expected a .set option");
        return;
    }
    if (option == "reorder") {
        if (options_.noreorder) end_noreorder(as);
        options_.noreorder = false;
    } else if (option == "noreorder") {
        if (!options_.noreorder) start_noreorder(as);
        options_.noreorder = true;
        noreorder_ = true;
    } else if (option == "macro") {
        options_.macro = true;
    } else if (option == "nomacro") {
        options_.macro = false;
    } else if (option == "push") {
        stack_.push_back(options_);
    } else if (option == "pop") {
        if (stack_.empty()) {
            as.error(location, ".set pop without a matching .set push");
            return;
        }
        const auto restored = stack_.back();
        stack_.pop_back();
        if (restored.noreorder && !options_.noreorder) {
            start_noreorder(as);
            noreorder_ = true;
        } else if (!restored.noreorder && options_.noreorder) {
            end_noreorder(as);
        }
        options_ = restored;
    } else if (option == "mips0" || option == "arch=default") {
        options_.architecture = file_.architecture;
        options_.gp64 = file_.gp64;
        options_.fpr = file_.fpr;
    } else if (option == "gp=default") {
        options_.gp64 = file_.gp64;
    } else if (option == "fp=default") {
        options_.fpr = file_.fpr;
    } else {
        switch (code_option(as, option, location, options_)) {
        case OptionResult::Unknown:
            as.error(location, "unsupported .set option '" + option + "'");
            return;
        case OptionResult::Failed:
        case OptionResult::Handled:
            return;
        case OptionResult::Architecture: {
            // As in GNU as, selecting an ISA fixes the register widths it
            // has.
            const auto isa = options_.architecture.isa;
            if (isa == isa_mips1) options_.fpr = FprMode::Fp32;
            options_.gp64 = has_64bit_registers(isa);
            if (options_.fpr != FprMode::Fpxx) {
                options_.fpr = options_.gp64 ? FprMode::Fp64 : FprMode::Fp32;
            }
            return;
        }
        }
    }
}

void MipsTarget::module_directive(Assembler& as, Statement& statement) {
    auto& cursor = statement.operands;
    const auto option = lower(trimmed(cursor.rest()));
    const auto location = statement.location;
    if (started_) {
        as.error(location, ".module is not permitted after code or a .set directive");
        return;
    }
    auto settings = options_;
    if (option == "softfloat" || option == "hardfloat") {
        model_.soft_float = option == "softfloat";
    } else if (option == "singlefloat" || option == "doublefloat") {
        model_.single_float = option == "singlefloat";
    } else {
        switch (code_option(as, option, location, settings)) {
        case OptionResult::Unknown:
            as.error(location, "unsupported .module option '" + option + "'");
            return;
        case OptionResult::Failed:
            return;
        case OptionResult::Handled:
            if (option.starts_with("gp=")) gp_explicit_ = true;
            if (option.starts_with("fp=")) fp_explicit_ = true;
            if (option.ends_with("oddspreg")) odd_spreg_explicit_ = true;
            break;
        case OptionResult::Architecture:
            // The file's widths follow the new ISA unless given.
            if (!gp_explicit_) {
                settings.gp64 = has_64bit_registers(settings.architecture.isa) &&
                                abi_gpr_bits(subtarget_.abi_info()) == 64;
            }
            if (!fp_explicit_ && settings.fpr != FprMode::Fpxx) {
                settings.fpr = settings.gp64 ? FprMode::Fp64 : FprMode::Fp32;
            }
            break;
        }
    }
    if (!odd_spreg_explicit_) {
        settings.odd_spreg = settings.fpr != FprMode::Fpxx && !model_.soft_float;
    }
    // GNU as checks the file's widths against the ABI's registers.
    if (abi_gpr_bits(subtarget_.abi_info()) == 32) {
        if (settings.gp64) {
            as.error(location, "gp=64 needs an ABI with 64-bit registers, not " +
                                   std::string(subtarget_.abi()));
            return;
        }
        if (option == "fp=64") {
            as.warning(location, "fp=64 with the 32-bit registers of the " +
                                     std::string(subtarget_.abi()) + " ABI");
        }
    }
    options_ = settings;
    file_ = settings;
    model_.architecture = file_.architecture;
    model_.isa64 = has_64bit_registers(file_.architecture.isa);
    model_.gp64 = file_.gp64;
    model_.fpr_mode = file_.fpr;
    model_.odd_spreg = file_.odd_spreg;
    gnu_floating_abi(model_);
}

void MipsTarget::end_procedure(Assembler& as, Statement& statement) {
    auto& cursor = statement.operands;
    std::optional<assembler::SymbolId> symbol = procedure_.symbol;
    if (!cursor.at_end()) {
        const auto name = cursor.symbol_name();
        if (!name) {
            as.error(cursor.location(), "expected a symbol name");
            return;
        }
        symbol = as.symbol(*name, statement.location);
    }
    if (!cursor.at_end()) {
        as.error(cursor.location(), "unexpected text after .end");
        return;
    }
    if (!symbol) {
        as.error(statement.location, ".end without a procedure name");
        return;
    }
    // The size ends before any hazard nops the end of the code needs.
    const auto end = as.temporary_label();
    flush(as);
    const auto pdr = as.section(".pdr", elf::sht_progbits, 0);
    auto& section = as.section(pdr);
    section.alignment = std::max<std::uint64_t>(section.alignment, 4);
    as.push_section(pdr);
    as.emit_value(as.symbol_reference(*symbol, statement.location), 4, statement.location);
    for (const auto value : {std::int64_t{procedure_.gpr_mask}, procedure_.gpr_offset,
                             std::int64_t{procedure_.fpr_mask}, procedure_.fpr_offset,
                             procedure_.frame_offset, std::int64_t{procedure_.frame_register},
                             std::int64_t{procedure_.return_register}}) {
        as.emit_integer(static_cast<std::uint64_t>(value), 4);
    }
    as.pop_section(statement.location);
    // .end sets the size; a later .size replaces it.
    as.symbol(*symbol).size = as.binary(assembler::Operator::Subtract,
                                        as.symbol_reference(end, statement.location),
                                        as.symbol_reference(*symbol, statement.location));
    procedure_ = {};
}

void MipsTarget::align_with_labels(Assembler& as, std::uint64_t alignment,
                                   std::optional<std::uint8_t> fill,
                                   const std::vector<SymbolId>& labels) {
    as.emit_alignment(alignment, fill, 0);
    for (const auto label : labels) as.move_label(label);
}

void MipsTarget::align_data(Assembler& as, std::string_view name) {
    unsigned size = 0;
    if (name == ".half" || name == ".hword" || name == ".short") size = 2;
    else if (name == ".word" || name == ".int" || name == ".long" || name == ".float" ||
             name == ".single") size = 4;
    else if (name == ".dword" || name == ".quad" || name == ".double") size = 8;
    else return;
    const auto labels = labels_;
    flush(as);
    if (auto_align_) align_with_labels(as, size, std::nullopt, labels);
}

bool MipsTarget::directive(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    if (name == ".set") {
        set_directive(as, statement);
        return true;
    }
    if (name == ".module") {
        module_directive(as, statement);
        return true;
    }
    if (model_.gnu && name == ".file") {
        // GNU as on MIPS names the file with the first .file, numbered or
        // not; the numbered form also enters the line table.
        auto probe = cursor;
        probe.skip_space();
        const bool numbered = probe.peek() != '"';
        if (numbered && !as.parse_absolute(probe)) return true;
        const auto file = probe.string_literal(as.diagnostics());
        if (!file) return false;
        if (!file_seen_) as.add_file_symbol(*file);
        file_seen_ = true;
        return !numbered;
    }
    if (name == ".option") {
        const auto option = cursor.identifier();
        if (option && *option == "pic0") pic_ = false;
        else if (option && *option == "pic2") pic_ = true;
        else as.error(statement.location, "unsupported .option");
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after .option");
        return true;
    }
    if (name == ".abicalls") {
        pic_ = true;
        return true;
    }
    if (name == ".nan") {
        const auto mode = cursor.identifier();
        cursor.skip_space();
        const auto rest = cursor.rest();
        if (mode && *mode == "legacy") {
            nan2008_ = false;
        } else if (!mode && rest.starts_with("2008")) {
            cursor.set_position(cursor.position() + 4);
            nan2008_ = true;
        } else {
            as.error(statement.location, "expected '.nan 2008' or '.nan legacy'");
        }
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after .nan");
        return true;
    }
    if (name == ".ent") {
        const auto symbol_name = cursor.symbol_name();
        if (!symbol_name) {
            as.error(cursor.location(), "expected a symbol name");
            return true;
        }
        if (cursor.consume(",")) (void)as.parse_absolute(cursor);
        procedure_ = {};
        procedure_.symbol = as.symbol(*symbol_name, statement.location);
        as.symbol(*procedure_.symbol).type = elf::stt_func;
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after .ent");
        return true;
    }
    if (name == ".end") {
        end_procedure(as, statement);
        return true;
    }
    if (name == ".frame") {
        const auto frame = gpr(as, cursor);
        if (!frame || !comma(as, cursor)) return true;
        const auto size = as.parse_absolute(cursor);
        if (!size || !comma(as, cursor)) return true;
        const auto return_register = gpr(as, cursor);
        if (!return_register) return true;
        procedure_.frame_register = *frame;
        procedure_.frame_offset = *size;
        procedure_.return_register = *return_register;
        if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after .frame");
        return true;
    }
    if (name == ".mask" || name == ".fmask") {
        const auto mask = as.parse_absolute(cursor);
        if (!mask || !comma(as, cursor)) return true;
        const auto offset = as.parse_absolute(cursor);
        if (!offset) return true;
        if (name == ".mask") {
            procedure_.gpr_mask = static_cast<std::uint32_t>(*mask);
            procedure_.gpr_offset = *offset;
        } else {
            procedure_.fpr_mask = static_cast<std::uint32_t>(*mask);
            procedure_.fpr_offset = *offset;
        }
        return true;
    }
    if (name == ".rdata" || name == ".sdata" || name == ".sbss") {
        flush(as);
        std::int64_t subsection = 0;
        if (!cursor.at_end()) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return true;
            subsection = *value;
        }
        const auto section = name == ".rdata"
            ? as.section(".rodata", elf::sht_progbits, elf::shf_alloc)
            : as.section(name, name == ".sbss" ? elf::sht_nobits : elf::sht_progbits,
                         elf::shf_alloc | elf::shf_write);
        as.switch_section(section, subsection);
        auto_align_ = true;
        return true;
    }
    if (name == ".text" || name == ".data" || name == ".bss" || name == ".section" ||
        name == ".pushsection" || name == ".popsection" || name == ".previous" ||
        name == ".subsection") {
        auto_align_ = true;
        return false;
    }
    if (model_.gnu && name == ".align") {
        // GNU as's MIPS .align also moves the labels before it, and
        // `.align 0` stops data directives from aligning.
        const auto labels = labels_;
        flush(as);
        const auto location = cursor.location();
        const auto amount = as.parse_absolute(cursor);
        if (!amount) return true;
        std::optional<std::uint8_t> fill;
        if (cursor.consume(",")) {
            const auto value = as.parse_absolute(cursor);
            if (!value) return true;
            fill = static_cast<std::uint8_t>(*value);
        }
        if (!cursor.at_end()) {
            as.error(cursor.location(), "unexpected text after .align");
            return true;
        }
        if (*amount < 0 || *amount > 28) {
            as.error(location, "alignment power is out of range [0, 28]");
            return true;
        }
        auto_align_ = *amount != 0;
        if (*amount != 0) align_with_labels(as, std::uint64_t{1} << *amount, fill, labels);
        return true;
    }
    if (model_.gnu) align_data(as, name);
    if (name == ".cpload" || name == ".cprestore" || name == ".cpsetup" || name == ".cplocal" ||
        name == ".cpreturn" || name == ".cpadd" || name == ".gpword" || name == ".gpdword" ||
        name == ".insn" || name == ".mips16" || name == ".nomips16" || name == ".micromips" ||
        name == ".nomicromips") {
        as.error(statement.location, "unsupported MIPS directive '" + name + "'");
        return true;
    }
    return false;
}

void MipsTarget::finish(Assembler& as) {
    // .text, .data, and .bss always exist; LLVM MC aligns them to 16 bytes,
    // GNU as for embedded targets to what they hold.
    for (const auto& [name, type, flags] :
         {std::tuple{std::string_view{".text"}, elf::sht_progbits, elf::shf_alloc | elf::shf_execinstr},
          std::tuple{std::string_view{".data"}, elf::sht_progbits, elf::shf_alloc | elf::shf_write},
          std::tuple{std::string_view{".bss"}, elf::sht_nobits, elf::shf_alloc | elf::shf_write}}) {
        auto& section = as.section(as.section(name, type, flags));
        if (!model_.gnu) section.alignment = std::max<std::uint64_t>(section.alignment, 16);
    }
    if (model_.gnu) {
        // GNU as describes procedures in .pdr even when there are none.
        auto& pdr = as.section(as.section(".pdr", elf::sht_progbits, 0));
        pdr.alignment = std::max<std::uint64_t>(pdr.alignment, 4);
        // It pads the end of a code section to the section's alignment with
        // nops, gives pointer arrays a pointer-sized entry, and keeps the
        // other sections as large as their contents.
        for (std::size_t index = 0; index < as.sections().size(); ++index) {
            auto& section = as.section(assembler::SectionId{static_cast<std::uint32_t>(index)});
            if (section.type == elf::sht_init_array || section.type == elf::sht_fini_array ||
                section.type == elf::sht_preinit_array) {
                section.entry_size = model_.elf64 ? 8 : 4;
            }
            if ((section.flags & elf::shf_execinstr) == 0 || section.alignment <= 1) continue;
            assembler::Fragment padding;
            padding.kind = assembler::Fragment::Kind::Align;
            padding.alignment = section.alignment;
            padding.code_fill = true;
            for (const auto& fragment : section.fragments) {
                padding.subsection = std::max(padding.subsection, fragment.subsection);
            }
            section.fragments.push_back(std::move(padding));
        }
    }
    // GNU as for embedded targets keeps the register usage record out of
    // the image.
    if (model_.elf64) {
        const auto id = as.section(".MIPS.options", sht_mips_options,
                                   (model_.gnu ? 0 : elf::shf_alloc) | shf_mips_nostrip, 1);
        as.section(id).alignment = 8;
        as.push_section(id);
        as.emit_integer(1, 1);  // ODK_REGINFO
        as.emit_integer(40, 1);
        as.emit_integer(0, 2);
        as.emit_integer(0, 4);
        as.emit_integer(gpr_mask_, 4);
        as.emit_integer(0, 4);
        for (const auto mask : cpr_mask_) as.emit_integer(mask, 4);
        as.emit_integer(0, 8);
        as.pop_section({});
    } else {
        const auto id = model_.gnu ? as.section(".reginfo", sht_mips_reginfo, 0, 1)
                                   : as.section(".reginfo", sht_mips_reginfo, elf::shf_alloc, 24);
        as.section(id).alignment = 4;
        as.push_section(id);
        as.emit_integer(gpr_mask_, 4);
        for (const auto mask : cpr_mask_) as.emit_integer(mask, 4);
        as.emit_integer(0, 4);
        as.pop_section({});
    }
    const auto& level = isa_level(model_.architecture.isa);
    const auto id = as.section(".MIPS.abiflags", sht_mips_abiflags, elf::shf_alloc, 24);
    as.section(id).alignment = 8;
    as.push_section(id);
    as.emit_integer(0, 2);
    as.emit_integer(level.level, 1);
    as.emit_integer(level.revision, 1);
    as.emit_integer(model_.gp64 ? 2 : 1, 1);
    as.emit_integer(model_.cpr1_size, 1);
    as.emit_integer(0, 1);
    as.emit_integer(model_.fp_abi, 1);
    as.emit_integer(0, 4);
    as.emit_integer(0, 4);
    const bool odd_flag = model_.gnu ? (model_.architecture.odd_single ||
                                        model_.fpr_mode == FprMode::Fp64) &&
                                           model_.odd_spreg
                                     : model_.odd_spreg;
    as.emit_integer(odd_flag ? 1 : 0, 4);
    as.emit_integer(0, 4);
    as.pop_section({});
    if (model_.gnu && model_.fp_abi != 0) {
        // The GNU object attribute that records the FP ABI:
        // Tag_GNU_MIPS_ABI_FP in the "gnu" vendor subsection.
        const auto attributes = as.section(".gnu.attributes", elf::sht_gnu_attributes, 0);
        as.push_section(attributes);
        as.emit_integer('A', 1);
        as.emit_integer(15, 4);
        static constexpr std::array<std::uint8_t, 4> vendor{'g', 'n', 'u', 0};
        as.emit_bytes(vendor);
        as.emit_integer(1, 1);  // Tag_File
        as.emit_integer(7, 4);
        as.emit_integer(4, 1);  // Tag_GNU_MIPS_ABI_FP
        as.emit_integer(model_.fp_abi, 1);
        as.pop_section({});
    }
}

bool MipsTarget::apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                             std::span<std::uint8_t> bytes, bool relocated) const {
    const auto word = static_cast<std::uint32_t>(as.read_integer(bytes));
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    std::uint32_t field = 0;
    std::uint32_t mask = 0xffff;
    switch (fixup.kind.value) {
    case fixup_hi16: field = high16(unsigned_value); break;
    case fixup_lo16: field = low16(unsigned_value); break;
    case fixup_gprel16:
        if (!relocated) {
            as.error(fixup.location, "%gp_rel needs a symbol");
            return false;
        }
        if (!model_.elf64 && !is_int16(value)) {
            as.error(fixup.location, "the %gp_rel addend " + std::to_string(value) +
                                         " does not fit the 16-bit field of an o32 relocation");
            return false;
        }
        field = low16(unsigned_value);
        break;
    case fixup_higher: field = higher16(unsigned_value); break;
    case fixup_highest: field = highest16(unsigned_value); break;
    case fixup_jump26:
        if (!relocated && (value & 3) != 0) {
            as.error(fixup.location, "jump target is not word aligned");
            return false;
        }
        field = jump26(unsigned_value);
        mask = jump26_mask;
        break;
    case fixup_branch16: {
        const auto displacement = value - 4;
        if (!relocated && (displacement & 3) != 0) {
            as.error(fixup.location, "branch target is not word aligned");
            return false;
        }
        const auto offset = displacement / 4;
        if (!is_int16(offset)) {
            as.error(fixup.location, "branch target is out of range");
            return false;
        }
        field = static_cast<std::uint32_t>(offset) & 0xffffU;
        break;
    }
    case fixup_signed16:
    case fixup_unsigned16: {
        const bool is_signed = fixup.kind.value == fixup_signed16;
        if (is_signed ? !is_int16(value) : !is_uint16(value)) {
            as.error(fixup.location, "immediate " + std::to_string(value) + " is out of range " +
                                         (is_signed ? "[-32768, 32767]" : "[0, 65535]"));
            return false;
        }
        field = static_cast<std::uint32_t>(value) & 0xffffU;
        break;
    }
    default:
        return false;
    }
    as.write_integer(bytes, insert_field(word, field, mask));
    return true;
}

std::optional<assembler::RelocationChoice> MipsTarget::relocation(
    Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
    const assembler::Symbol& symbol, const assembler::Section* symbol_section) const {
    const bool merge = symbol_section && (symbol_section->flags & elf::shf_merge) != 0;
    switch (fixup.kind.value) {
    case 1:
        as.error(fixup.location, "MIPS has no one-byte relocation");
        return std::nullopt;
    case 2:
        // As in GNU as: the n64 class has a 2-byte data relocation; o32 has
        // none (R_MIPS_16 relocates a word's low half).
        if (model_.elf64) return assembler::RelocationChoice{r_mips_rel16};
        as.error(fixup.location, "an o32 object has no relocation for a 2-byte field");
        return std::nullopt;
    case 4: return assembler::RelocationChoice{r_mips_32};
    case 8: return assembler::RelocationChoice{r_mips_64};
    case assembler::fixup::data4_pc.value: return assembler::RelocationChoice{r_mips_pc32};
    // REL HI16/LO16 pairs carry the offset across both halves, which a
    // linker must reassemble to find mergeable data; keep the symbol.
    case fixup_hi16: return assembler::RelocationChoice{r_mips_hi16, merge && !model_.elf64};
    case fixup_lo16: return assembler::RelocationChoice{r_mips_lo16, merge && !model_.elf64};
    case fixup_gprel16: {
        // A REL addend has 16 bits: a local symbol farther into its section
        // keeps its name rather than becoming the section plus offset.
        const bool far = !model_.elf64 && symbol.section &&
                         !is_int16(static_cast<std::int64_t>(as.address(symbol)) + value.constant);
        return assembler::RelocationChoice{r_mips_gprel16, far};
    }
    case fixup_higher: return assembler::RelocationChoice{r_mips_higher};
    case fixup_highest: return assembler::RelocationChoice{r_mips_highest};
    case fixup_jump26: return assembler::RelocationChoice{r_mips_26};
    case fixup_branch16: return assembler::RelocationChoice{r_mips_pc16, false, -4};
    default:
        as.error(fixup.location, "this operand must be absolute; use a relocation operator such as %lo");
        return std::nullopt;
    }
}

void MipsTarget::order_relocations(std::vector<assembler::Relocation>& relocations) const {
    // A REL HI16 must precede a LO16 of the same symbol and addend, the
    // linker's rule for reassembling the split addend.
    if (model_.gnu) {
        gnu_order(relocations);
        return;
    }
    std::stable_sort(relocations.begin(), relocations.end(),
                     [](const assembler::Relocation& left, const assembler::Relocation& right) {
                         return left.offset < right.offset;
                     });
    // As LLVM MC does, an unmatched HI16 moves before the LO16 with the
    // lowest addend no less than its own, preferring an unmatched one.
    struct Entry {
        assembler::Relocation relocation;
        bool matched{};
    };
    std::list<Entry> sorted;
    std::vector<std::list<Entry>::iterator> highs;
    for (const auto& relocation : relocations) {
        sorted.push_back({relocation});
        if (relocation.type == r_mips_hi16) highs.push_back(std::prev(sorted.end()));
    }
    for (const auto high : highs) {
        const auto& r = high->relocation;
        const auto next = std::next(high);
        if (next != sorted.end() && next->relocation.type == r_mips_lo16 &&
            next->relocation.symbol == r.symbol && next->relocation.addend == r.addend) {
            continue;
        }
        auto best = sorted.end();
        for (auto candidate = sorted.begin(); candidate != sorted.end(); ++candidate) {
            const auto& low = candidate->relocation;
            if (low.type == r_mips_lo16 && low.symbol == r.symbol && r.addend <= low.addend &&
                (best == sorted.end() || low.addend < best->relocation.addend ||
                 (!best->matched && low.addend == best->relocation.addend))) {
                best = candidate;
            }
        }
        if (best != sorted.end() && r.addend == best->relocation.addend) best->matched = true;
        sorted.splice(best, sorted, high);
    }
    std::size_t index = 0;
    for (const auto& entry : sorted) relocations[index++] = entry.relocation;
}

void MipsTarget::gnu_order(std::vector<assembler::Relocation>& relocations) {
    // The section's fixups in creation order; a fixup resolved in place is
    // a gap, which separates a HI16 from the LO16 after it.
    struct Link {
        const assembler::Relocation* relocation{};
        std::int64_t addend{};
    };
    std::list<Link> chain;
    std::uint32_t next = 1;
    for (const auto& relocation : relocations) {
        for (; next < relocation.sequence; ++next) chain.push_back({});
        chain.push_back({&relocation, relocation.target_addend});
        next = relocation.sequence + 1;
    }
    using Position = std::list<Link>::iterator;
    const auto is = [](const Link& link, std::uint32_t type) {
        return link.relocation && link.relocation->type == type;
    };
    const auto matched = [&](Position high) {
        const auto after = std::next(high);
        return after != chain.end() && is(*after, r_mips_lo16) &&
               after->relocation->target == high->relocation->target &&
               after->addend == high->addend;
    };
    // A HI16 whose predecessor in the list has its LO16 next replaces it.
    std::vector<Position> highs;
    for (auto position = chain.begin(); position != chain.end(); ++position) {
        if (!is(*position, r_mips_hi16)) continue;
        if (!highs.empty() && matched(highs.back())) highs.back() = position;
        else highs.push_back(position);
    }
    for (auto each = highs.rbegin(); each != highs.rend(); ++each) {
        const auto high = *each;
        if (matched(high)) continue;
        // The LO16 with the lowest addend no less than the HI16's; of equal
        // ones the last that no matched HI16 precedes.
        auto low = chain.end();
        bool claimed = false;
        for (auto position = chain.begin(); position != chain.end(); ++position) {
            if (is(*position, r_mips_lo16) &&
                position->relocation->target == high->relocation->target &&
                position->addend >= high->addend &&
                (low == chain.end() || position->addend < low->addend ||
                 (!claimed && position->addend == low->addend))) {
                low = position;
            }
            claimed = is(*position, r_mips_hi16) && matched(position);
        }
        if (low == chain.end()) continue;
        high->addend = low->addend;
        if (std::next(high) != low) chain.splice(low, chain, high);
    }
    std::vector<assembler::Relocation> ordered;
    ordered.reserve(relocations.size());
    for (const auto& link : chain) {
        if (link.relocation) ordered.push_back(*link.relocation);
    }
    relocations = std::move(ordered);
}

} // namespace

std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, const Subtarget& subtarget,
    const assembler::Options& options, SourceManager& manager, Diagnostics& diagnostics) {
    if (subtarget.has_feature(Feature::AbiCalls)) {
        diagnostics.command_error("MIPS PIC objects (-mabicalls) are not supported");
        return std::nullopt;
    }
    if (subtarget.has_feature(Feature::Mips16) || subtarget.has_feature(Feature::MicroMips)) {
        diagnostics.command_error("MIPS16 and microMIPS encodings are not supported");
        return std::nullopt;
    }
    MipsTarget target(model_for(subtarget, options.compiler_output), subtarget, options.optimize);
    return assembler::assemble(sources, target, options, manager, diagnostics);
}

} // namespace cross::mips
