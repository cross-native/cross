// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/assembler.hpp"

#include "assembler/assembler.hpp"
#include "target/mips/elf.hpp"
#include "target/mips/features.hpp"
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
namespace elf = assembler::elf;

// ISA levels as bits; each instruction lists the levels that have it.
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
    // A CPU extension rather than a level.
    isa_allegrex = 1U << 9,
};
constexpr std::uint16_t I1 = 0x1ff;
constexpr std::uint16_t I2 = I1 & ~isa_mips1;
constexpr std::uint16_t I3 = isa_mips3 | isa_mips4 | isa_mips5 | isa_mips64 | isa_mips64r2;
constexpr std::uint16_t I4 = isa_mips4 | isa_mips5 | isa_mips64 | isa_mips64r2;
constexpr std::uint16_t I32 = isa_mips32 | isa_mips32r2 | isa_mips64 | isa_mips64r2;
constexpr std::uint16_t I33 = isa_mips32r2 | isa_mips64r2;
constexpr std::uint16_t I64 = isa_mips64 | isa_mips64r2;
constexpr std::uint16_t I3_32 = I3 | I32;
constexpr std::uint16_t I4_32 = I4 | I32;
constexpr std::uint16_t I3_33 = I3 | I33;
constexpr std::uint16_t ALX = isa_allegrex;

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

enum class Syntax : std::uint8_t {
    RdRsRt, RdRtRs, RdRtSa, RtRsSimm, RtRsUimm, RtUimm, RdRsCount, RdRt,
    RdRsCc, RsRt, Divide, Rd, Rs, RtMemory, FtMemory, CacheMemory,
    RsRtBranch, RsBranch, BranchAlways, CcBranch, Jump, JumpRegister,
    JumpLinkRegister, FdFsFt, FdFs, FsFt, FdFsRt, FdFsCc, RtFs, RtControl,
    RtCoprocessor, NoOperands, SyncType, BreakCode, SyscallCode,
    LoadImmediate, LoadImmediate64, MoveMacro, Negate, NotMacro,
};

struct Description {
    std::string_view mnemonic;
    Syntax syntax;
    std::uint32_t word;
    std::uint16_t isa;
};

constexpr Description descriptions[] = {
#define X(name, spelling, latency, syntax, word, isa) \
    {spelling, Syntax::syntax, word, isa},
#include "target/mips/instructions.def"
#undef X
};

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

struct Model {
    bool elf64{};
    bool big_endian{};
    Abi abi{};
    std::uint16_t isa{isa_mips1};
    bool gp64{};
    bool allegrex{};
    // .MIPS.abiflags facts: Val_GNU_MIPS_ABI_FP_*, AFL_REG_* of the FPRs,
    // and AFL_FLAGS1_ODDSPREG.
    std::uint8_t fp_abi{1};
    std::uint8_t cpr1_size{1};
    bool odd_spreg{};
    // 64-bit FPRs (FR=1): doubles may use odd registers.
    bool fr1{};
    bool nan2008{};
};

bool doubleword_fpu_memory(const Subtarget& subtarget) {
    if (!subtarget.has_feature(Feature::Mips2)) return false;
    const auto load = find_instruction_cost(subtarget.target(), "ldc1");
    const auto store = find_instruction_cost(subtarget.target(), "sdc1");
    return load && store && subtarget.has_instruction(*load) &&
           subtarget.has_instruction(*store);
}

Model model_for(const Subtarget& subtarget, bool compiler_output) {
    const auto has = [&](Feature feature) { return subtarget.has_feature(feature); };
    Model model;
    model.elf64 = subtarget.abi_info().address_bits > 32;
    model.big_endian = subtarget.target().data_layout.byte_order == ByteOrder::Big;
    model.abi = model.elf64 ? Abi::N64
                : subtarget.abi_info().elf_abi_tag == "eabi32" ? Abi::Eabi32
                                                               : Abi::O32;
    model.isa = has(Feature::Mips64)     ? isa_mips64
                : has(Feature::Mips32r2) ? isa_mips32r2
                : has(Feature::Mips32)   ? isa_mips32
                : has(Feature::Mips5)    ? isa_mips5
                : has(Feature::Mips4)    ? isa_mips4
                : has(Feature::Mips3)    ? isa_mips3
                : has(Feature::Mips2)    ? isa_mips2
                                         : isa_mips1;
    model.gp64 = has(Feature::Mips3);
    model.allegrex = has(Feature::Allegrex);
    model.nan2008 = has(Feature::Nan2008);
    model.odd_spreg = true;
    if (has(Feature::SoftFloat)) {
        model.fp_abi = 3;
        model.cpr1_size = 0;
    } else if (model.elf64) {
        // The n64 object class is defined on a 64-bit FPU.
        model.cpr1_size = 2;
        model.fr1 = true;
    } else if (has(Feature::SingleFloat)) {
        model.fp_abi = 2;
    } else if (has(Feature::Fp64)) {
        // With 64-bit FPRs every register holds a single, as GNU as assumes.
        model.cpr1_size = 2;
        model.fr1 = true;
        model.fp_abi = 6;
    } else if (has(Feature::Fpxx) || (compiler_output && has(Feature::Fp32) &&
                                      doubleword_fpu_memory(subtarget))) {
        // Compiled code keeps to the FPXX subset of the paired-register
        // convention, so it links with either FPU. Without ldc1 and sdc1,
        // as on MIPS I, f64 halves travel through odd single registers and
        // the code stays FP32.
        model.fp_abi = 5;
        model.odd_spreg = false;
    }
    if (!compiler_output) {
        model.odd_spreg = model.fp_abi != 5 && !has(Feature::SoftFloat) &&
                          (model.fr1 || has(Feature::OddSpreg));
    }
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

class MipsTarget final : public assembler::Target {
public:
    explicit MipsTarget(const Model& model) : model_(model) {
        options_.isa = model.isa;
        for (std::size_t index = 0; index < std::size(descriptions); ++index) {
            mnemonics_.emplace(descriptions[index].mnemonic, index);
        }
        nan2008_ = model.nan2008;
    }

    [[nodiscard]] assembler::ObjectInfo object() const override {
        return {model_.elf64, model_.big_endian, 8, model_.elf64};
    }

    [[nodiscard]] std::uint32_t elf_flags() const override {
        std::uint32_t flags = isa_level(model_.isa).elf_arch;
        if (model_.abi == Abi::O32) flags |= ef_mips_abi_o32;
        if (model_.abi == Abi::Eabi32) flags |= ef_mips_abi_eabi32;
        if (model_.gp64 && !model_.elf64) flags |= ef_mips_32bitmode;
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
    void finish(Assembler& as) override;
    bool apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                     std::span<std::uint8_t> bytes, bool relocated) const override;
    std::optional<assembler::RelocationChoice> relocation(
        Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
        const assembler::Symbol& symbol, const assembler::Section* symbol_section) const override;
    void order_relocations(std::vector<assembler::Relocation>& relocations) const override;

private:
    struct Settings {
        std::uint16_t isa{};
        bool reorder{true};
        bool at{true};
        bool macro{true};
    };

    // Operands.
    std::optional<unsigned> gpr(Assembler& as, Cursor& cursor);
    std::optional<unsigned> fpr(Assembler& as, Cursor& cursor);
    std::optional<unsigned> condition_code(Cursor& cursor);
    bool comma(Assembler& as, Cursor& cursor);
    std::optional<Immediate> immediate(Assembler& as, Cursor& cursor);
    bool sixteen(Assembler& as, std::uint32_t& word, const Immediate& value, bool is_signed,
                 std::optional<PendingFixup>& fixup);
    std::optional<std::int64_t> absolute(Assembler& as, Cursor& cursor, std::int64_t low,
                                         std::int64_t high, std::string_view what);

    // Emission and register-usage records.
    void emit(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup = {});
    void use_gpr(unsigned number) { gpr_mask_ |= 1U << number; }
    void use_fpr(unsigned number, bool wide) {
        cpr_mask_[1] |= 1U << number;
        // As LLVM MC does, a double marks both halves of its pair unless
        // the CPU has 64-bit FPRs (MIPS III or later) or the ABI uses them.
        if (wide && !model_.fr1 && !model_.gp64 && number < 31) {
            cpr_mask_[1] |= 1U << (number + 1);
        }
    }
    bool check_double(Assembler& as, unsigned number, SourceLocation location);
    // An arithmetic operand: odd registers hold doubles only with 64-bit
    // FPRs and singles only where odd single-precision registers exist.
    bool check_operand(Assembler& as, unsigned number, bool wide, SourceLocation location);
    void load_immediate(Assembler& as, unsigned target, std::int64_t value, bool wide,
                        SourceLocation location);
    bool available(std::uint16_t isa) const {
        return (isa & options_.isa) != 0 || ((isa & ALX) != 0 && model_.allegrex);
    }
    void set_directive(Assembler& as, Statement& statement);
    void end_procedure(Assembler& as, Statement& statement);

    Model model_;
    Settings options_;
    std::vector<Settings> stack_;
    std::unordered_map<std::string_view, std::size_t> mnemonics_;
    bool noreorder_{};
    bool pic_{};
    bool nan2008_{};
    std::uint32_t gpr_mask_{};
    std::array<std::uint32_t, 4> cpr_mask_{};
    unsigned emitted_{};
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

std::optional<unsigned> MipsTarget::fpr(Assembler& as, Cursor& cursor) {
    cursor.skip_space();
    const auto location = cursor.location();
    const auto rest = cursor.rest();
    if (rest.starts_with("$f") && !rest.starts_with("$fp") && !rest.starts_with("$fcc")) {
        std::size_t end = 2;
        while (end < rest.size() && rest[end] >= '0' && rest[end] <= '9') ++end;
        if (const auto number = small_number(rest.substr(2, end - 2), 31)) {
            cursor.set_position(cursor.position() + end);
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

bool MipsTarget::sixteen(Assembler& as, std::uint32_t& word, const Immediate& value,
                         bool is_signed, std::optional<PendingFixup>& fixup) {
    if (value.relocation) {
        fixup = PendingFixup{*value.relocation, value.expression, value.location};
        return true;
    }
    if (const auto number = as.absolute_now(value.expression)) {
        if (is_signed ? !is_int16(*number) : !is_uint16(*number)) {
            as.error(value.location, "immediate " + std::to_string(*number) + " is out of range " +
                                         (is_signed ? "[-32768, 32767]" : "[0, 65535]"));
            return false;
        }
        word |= static_cast<std::uint32_t>(*number) & 0xffffU;
        return true;
    }
    fixup = PendingFixup{is_signed ? fixup_signed16 : fixup_unsigned16, value.expression,
                         value.location};
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

void MipsTarget::emit(Assembler& as, std::uint32_t word,
                      const std::optional<PendingFixup>& fixup) {
    if (fixup) as.add_fixup(FixupKind{fixup->kind}, fixup->expression, fixup->location);
    as.emit_integer(word, 4);
    ++emitted_;
}

bool MipsTarget::check_operand(Assembler& as, unsigned number, bool wide,
                               SourceLocation location) {
    if (wide) return check_double(as, number, location);
    if (model_.fr1 || model_.odd_spreg || (number & 1U) == 0) return true;
    as.error(location, "a single-precision operation needs an even register without odd "
                       "single-precision registers, not $f" + std::to_string(number));
    return false;
}

bool MipsTarget::check_double(Assembler& as, unsigned number, SourceLocation location) {
    if (model_.fr1 || (number & 1U) == 0) return true;
    as.error(location, "a double-precision value needs an even register with 32-bit FPRs, not $f" +
                           std::to_string(number));
    return false;
}

void MipsTarget::load_immediate(Assembler& as, unsigned target, std::int64_t value, bool wide,
                                SourceLocation location) {
    // LLVM MC's expansion of li and dli, which GNU as matches for every
    // 32-bit value.
    const auto rt = static_cast<std::uint32_t>(target) << 16U;
    const auto rs_rt = (static_cast<std::uint32_t>(target) << 21U) | rt;
    const auto start = emitted_;
    const auto addiu = [&](std::int64_t immediate) {
        use_gpr(target);
        use_gpr(0);
        emit(as, 0x24000000U | rt | (static_cast<std::uint32_t>(immediate) & 0xffffU));
    };
    const auto ori_zero = [&](std::uint64_t immediate) {
        use_gpr(target);
        use_gpr(0);
        emit(as, 0x34000000U | rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto ori = [&](std::uint64_t immediate) {
        use_gpr(target);
        emit(as, 0x34000000U | rs_rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto lui = [&](std::uint64_t immediate) {
        use_gpr(target);
        emit(as, 0x3c000000U | rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto shift = [&](std::uint32_t function, unsigned amount) {
        use_gpr(target);
        emit(as, function | rt | (static_cast<std::uint32_t>(target) << 11U) |
                     ((amount & 31U) << 6U));
    };
    const auto dsll = [&](unsigned amount) {
        if (amount >= 32) shift(0x3c, amount - 32);
        else shift(0x38, amount);
    };
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
                    shift(0x3e, 0); // dsrl32
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
    if (!options_.macro && emitted_ - start > 1) {
        as.warning(location, "macro instruction expanded into multiple instructions");
    }
}

void MipsTarget::instruction(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    const auto found = mnemonics_.find(name);
    if (found == mnemonics_.end()) {
        as.error(statement.location, "unknown instruction '" + std::string(statement.name) + "'");
        return;
    }
    const auto& description = descriptions[found->second];
    auto& cursor = statement.operands;
    const auto location = statement.location;
    if (!available(description.isa)) {
        as.error(location, "instruction '" + name + "' is not available in " +
                               std::string(isa_level(options_.isa).name));
        return;
    }
    const auto rs = [](unsigned number) { return static_cast<std::uint32_t>(number) << 21U; };
    const auto rt = [](unsigned number) { return static_cast<std::uint32_t>(number) << 16U; };
    const auto rd = [](unsigned number) { return static_cast<std::uint32_t>(number) << 11U; };
    const auto sa = [](unsigned number) { return static_cast<std::uint32_t>(number) << 6U; };
    std::uint32_t word = description.word;
    std::optional<PendingFixup> fixup;
    bool branch = false;
    // The position of the next operand.
    const auto here = [&]() {
        cursor.skip_space();
        return cursor.location();
    };
    const auto finish = [&]() -> bool {
        if (!cursor.at_end()) {
            as.error(cursor.location(), "unexpected text after operands");
            return false;
        }
        return true;
    };
    // Floating formats: S, D, W, L from the fmt field and the operation.
    const auto format = (description.word >> 21U) & 0x1fU;
    const bool source_double = format == 17 || format == 21;
    const auto function = description.word & 0x3fU;
    const auto destination_double = [&]() {
        if (function == 0x20) return false;               // cvt.s
        if (function == 0x21) return true;                // cvt.d
        if (function == 0x24 || function == 0x0c || function == 0x0d ||
            function == 0x0e || function == 0x0f) return false;  // to word
        if (function == 0x25 || (function >= 0x08 && function <= 0x0b)) return true;  // to long
        return source_double;
    };

    switch (description.syntax) {
    case Syntax::RdRsRt:
    case Syntax::RdRtRs: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto a = gpr(as, cursor);
        if (!a || !comma(as, cursor)) return;
        const auto b = gpr(as, cursor);
        if (!b || !finish()) return;
        word |= rd(*d) | (description.syntax == Syntax::RdRsRt ? rs(*a) | rt(*b) : rt(*a) | rs(*b));
        use_gpr(*d);
        use_gpr(*a);
        use_gpr(*b);
        break;
    }
    case Syntax::RdRtSa: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        const bool doubleword_shift = name == "dsll" || name == "dsrl" || name == "dsra";
        const auto amount = absolute(as, cursor, 0, doubleword_shift ? 63 : 31, "shift amount");
        if (!amount || !finish()) return;
        auto shift = static_cast<unsigned>(*amount);
        if (shift >= 32) {
            word += 4; // dsll/dsrl/dsra become their 32 forms
            shift -= 32;
        }
        word |= rd(*d) | rt(*t) | sa(shift);
        use_gpr(*d);
        use_gpr(*t);
        break;
    }
    case Syntax::RtRsSimm:
    case Syntax::RtRsUimm: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto value = immediate(as, cursor);
        if (!value || !finish()) return;
        if (!sixteen(as, word, *value, description.syntax == Syntax::RtRsSimm, fixup)) return;
        word |= rt(*t) | rs(*s);
        use_gpr(*t);
        use_gpr(*s);
        break;
    }
    case Syntax::RtUimm: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        const auto value = immediate(as, cursor);
        if (!value || !finish()) return;
        if (!sixteen(as, word, *value, false, fixup)) return;
        word |= rt(*t);
        use_gpr(*t);
        break;
    }
    case Syntax::RdRsCount: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !finish()) return;
        word |= rd(*d) | rt(*d) | rs(*s);
        use_gpr(*d);
        use_gpr(*s);
        break;
    }
    case Syntax::RdRt: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto t = gpr(as, cursor);
        if (!t || !finish()) return;
        word |= rd(*d) | rt(*t);
        use_gpr(*d);
        use_gpr(*t);
        break;
    }
    case Syntax::RdRsCc: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto cc = condition_code(cursor);
        if (!cc) {
            as.error(cursor.location(), "expected a condition code register $fcc0-$fcc7");
            return;
        }
        if (!finish()) return;
        word |= rd(*d) | rs(*s) | (static_cast<std::uint32_t>(*cc) << 18U);
        use_gpr(*d);
        use_gpr(*s);
        break;
    }
    case Syntax::RsRt: {
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto t = gpr(as, cursor);
        if (!t || !finish()) return;
        word |= rs(*s) | rt(*t);
        use_gpr(*s);
        use_gpr(*t);
        break;
    }
    case Syntax::Divide: {
        // The machine instruction names $zero as its destination; GNU as
        // and LLVM MC expand the other forms into checked macros.
        const auto first_location = here();
        const auto first = gpr(as, cursor);
        if (!first || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s) return;
        if (*first != 0 || !cursor.consume(",")) {
            as.error(first_location, "the '" + name + "' macro is not supported; write '" + name +
                                         " $zero, rs, rt' and read LO or HI");
            return;
        }
        const auto t = gpr(as, cursor);
        if (!t || !finish()) return;
        word |= rs(*s) | rt(*t);
        use_gpr(*s);
        use_gpr(*t);
        break;
    }
    case Syntax::Rd: {
        const auto d = gpr(as, cursor);
        if (!d || !finish()) return;
        word |= rd(*d);
        use_gpr(*d);
        break;
    }
    case Syntax::Rs:
    case Syntax::JumpRegister: {
        const auto s = gpr(as, cursor);
        if (!s || !finish()) return;
        word |= rs(*s);
        use_gpr(*s);
        branch = description.syntax == Syntax::JumpRegister;
        break;
    }
    case Syntax::JumpLinkRegister: {
        const auto first = gpr(as, cursor);
        if (!first) return;
        unsigned d = 31;
        unsigned s = *first;
        if (cursor.consume(",")) {
            const auto second = gpr(as, cursor);
            if (!second) return;
            d = *first;
            s = *second;
        }
        if (!finish()) return;
        word |= rd(d) | rs(s);
        use_gpr(d);
        use_gpr(s);
        branch = true;
        break;
    }
    case Syntax::RtMemory:
    case Syntax::FtMemory:
    case Syntax::CacheMemory: {
        unsigned target = 0;
        if (description.syntax == Syntax::RtMemory) {
            const auto t = gpr(as, cursor);
            if (!t) return;
            target = *t;
            use_gpr(target);
        } else if (description.syntax == Syntax::FtMemory) {
            const auto register_location = here();
            const auto t = fpr(as, cursor);
            if (!t) return;
            target = *t;
            const bool wide = name == "ldc1" || name == "sdc1";
            if (wide && !check_double(as, target, register_location)) return;
            use_fpr(target, wide);
        } else {
            const auto operation = absolute(as, cursor, 0, 31, "cache operation");
            if (!operation) return;
            target = static_cast<unsigned>(*operation);
        }
        if (!comma(as, cursor)) return;
        cursor.skip_space();
        unsigned base = 0;
        if (cursor.rest().starts_with("($")) {
            cursor.consume("(");
        } else {
            const auto offset = immediate(as, cursor);
            if (!offset) return;
            if (!sixteen(as, word, *offset, true, fixup)) return;
            if (!cursor.consume("(")) {
                if (!finish()) return;
                if (fixup) {
                    as.error(offset->location, "a memory operand with a symbol needs a base register");
                    return;
                }
                word |= rt(target);
                use_gpr(0);
                break;
            }
        }
        const auto b = gpr(as, cursor);
        if (!b) return;
        if (!cursor.consume(")")) {
            as.error(cursor.location(), "expected ')'");
            return;
        }
        if (!finish()) return;
        base = *b;
        word |= rt(target) | rs(base);
        use_gpr(base);
        break;
    }
    case Syntax::RsRtBranch:
    case Syntax::RsBranch:
    case Syntax::BranchAlways:
    case Syntax::CcBranch:
    case Syntax::Jump: {
        if (description.syntax == Syntax::RsRtBranch) {
            const auto s = gpr(as, cursor);
            if (!s || !comma(as, cursor)) return;
            const auto t = gpr(as, cursor);
            if (!t || !comma(as, cursor)) return;
            word |= rs(*s) | rt(*t);
            use_gpr(*s);
            use_gpr(*t);
        } else if (description.syntax == Syntax::RsBranch) {
            const auto s = gpr(as, cursor);
            if (!s || !comma(as, cursor)) return;
            word |= rs(*s);
            use_gpr(*s);
            // beqz and bnez compare with $zero.
            if ((description.word >> 26U) == 4 || (description.word >> 26U) == 5 ||
                (description.word >> 26U) == 20 || (description.word >> 26U) == 21) {
                use_gpr(0);
            }
        } else if (description.syntax == Syntax::CcBranch) {
            if (const auto cc = condition_code(cursor)) {
                if (*cc != 0 && !available(I4_32)) {
                    as.error(location, "condition codes other than $fcc0 need MIPS IV or MIPS32");
                    return;
                }
                word |= static_cast<std::uint32_t>(*cc) << 18U;
                if (!comma(as, cursor)) return;
            }
        } else if (description.syntax == Syntax::BranchAlways) {
            use_gpr(0);
        }
        const auto target_location = here();
        const auto target = as.parse_expression(cursor);
        if (!target || !finish()) return;
        fixup = PendingFixup{description.syntax == Syntax::Jump ? fixup_jump26 : fixup_branch16,
                             *target, target_location};
        branch = true;
        break;
    }
    case Syntax::FdFsFt: {
        const auto d_location = here();
        const auto d = fpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s_location = here();
        const auto s = fpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto t_location = here();
        const auto t = fpr(as, cursor);
        if (!t || !finish()) return;
        if (!check_operand(as, *d, source_double, d_location) ||
            !check_operand(as, *s, source_double, s_location) ||
            !check_operand(as, *t, source_double, t_location)) {
            return;
        }
        word |= sa(*d) | rd(*s) | rt(*t);
        use_fpr(*d, source_double);
        use_fpr(*s, source_double);
        use_fpr(*t, source_double);
        break;
    }
    case Syntax::FdFs: {
        const auto d_location = here();
        const auto d = fpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s_location = here();
        const auto s = fpr(as, cursor);
        if (!s || !finish()) return;
        const bool d_wide = destination_double();
        if (!check_operand(as, *d, d_wide, d_location) ||
            !check_operand(as, *s, source_double, s_location)) {
            return;
        }
        word |= sa(*d) | rd(*s);
        use_fpr(*d, d_wide);
        use_fpr(*s, source_double);
        break;
    }
    case Syntax::FsFt: {
        if (const auto cc = condition_code(cursor)) {
            if (*cc != 0 && !available(I4_32)) {
                as.error(location, "condition codes other than $fcc0 need MIPS IV or MIPS32");
                return;
            }
            word |= static_cast<std::uint32_t>(*cc) << 8U;
            if (!comma(as, cursor)) return;
        }
        const auto s_location = here();
        const auto s = fpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto t_location = here();
        const auto t = fpr(as, cursor);
        if (!t || !finish()) return;
        if (!check_operand(as, *s, source_double, s_location) ||
            !check_operand(as, *t, source_double, t_location)) {
            return;
        }
        word |= rd(*s) | rt(*t);
        use_fpr(*s, source_double);
        use_fpr(*t, source_double);
        break;
    }
    case Syntax::FdFsRt:
    case Syntax::FdFsCc: {
        const auto d_location = here();
        const auto d = fpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s_location = here();
        const auto s = fpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        if (description.syntax == Syntax::FdFsRt) {
            const auto t = gpr(as, cursor);
            if (!t) return;
            word |= rt(*t);
            use_gpr(*t);
        } else {
            const auto cc = condition_code(cursor);
            if (!cc) {
                as.error(cursor.location(), "expected a condition code register $fcc0-$fcc7");
                return;
            }
            word |= static_cast<std::uint32_t>(*cc) << 18U;
        }
        if (!finish()) return;
        if (!check_operand(as, *d, source_double, d_location) ||
            !check_operand(as, *s, source_double, s_location)) {
            return;
        }
        word |= sa(*d) | rd(*s);
        use_fpr(*d, source_double);
        use_fpr(*s, source_double);
        break;
    }
    case Syntax::RtFs: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        const auto s_location = here();
        const auto s = fpr(as, cursor);
        if (!s || !finish()) return;
        const bool high = name == "mfhc1" || name == "mthc1";
        if (high && !check_double(as, *s, s_location)) return;
        word |= rt(*t) | rd(*s);
        use_gpr(*t);
        use_fpr(*s, high);
        break;
    }
    case Syntax::RtControl: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        cursor.skip_space();
        const auto control_location = here();
        auto rest = cursor.rest();
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
            as.error(control_location, "expected a floating-point control register");
            return;
        }
        cursor.set_position(cursor.position() + length);
        if (!finish()) return;
        word |= rt(*t) | rd(*control);
        use_gpr(*t);
        break;
    }
    case Syntax::RtCoprocessor: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        cursor.skip_space();
        const auto register_location = here();
        const auto rest = cursor.rest();
        std::optional<unsigned> number;
        std::size_t end = 1;
        if (rest.starts_with("$")) {
            while (end < rest.size() && rest[end] >= '0' && rest[end] <= '9') ++end;
            number = small_number(rest.substr(1, end - 1), 31);
        }
        if (!number) {
            as.error(register_location, "expected a coprocessor 0 register $0-$31");
            return;
        }
        cursor.set_position(cursor.position() + end);
        unsigned select = 0;
        if (cursor.consume(",")) {
            const auto value = absolute(as, cursor, 0, 7, "register select");
            if (!value) return;
            select = static_cast<unsigned>(*value);
            if (select != 0 && !available(name.starts_with("d") ? I64 : I32)) {
                as.error(location, "a coprocessor 0 register select needs MIPS32 or MIPS64");
                return;
            }
        }
        if (!finish()) return;
        word |= rt(*t) | rd(*number) | select;
        use_gpr(*t);
        cpr_mask_[0] |= 1U << *number;
        break;
    }
    case Syntax::NoOperands:
        if (!finish()) return;
        if (name == "nop") use_gpr(0);
        break;
    case Syntax::SyncType:
        if (!cursor.at_end()) {
            const auto type = absolute(as, cursor, 0, 31, "sync type");
            if (!type) return;
            if (*type != 0 && !available(I32)) {
                as.error(location, "a sync type needs MIPS32 or MIPS64");
                return;
            }
            word |= sa(static_cast<unsigned>(*type));
        }
        if (!finish()) return;
        break;
    case Syntax::BreakCode:
        if (!cursor.at_end()) {
            const auto code = absolute(as, cursor, 0, 1023, "break code");
            if (!code) return;
            word |= static_cast<std::uint32_t>(*code) << 16U;
            if (cursor.consume(",")) {
                const auto second = absolute(as, cursor, 0, 1023, "break code");
                if (!second) return;
                word |= static_cast<std::uint32_t>(*second) << 6U;
            }
        }
        if (!finish()) return;
        break;
    case Syntax::SyscallCode:
        if (!cursor.at_end()) {
            const auto code = absolute(as, cursor, 0, 0xfffff, "syscall code");
            if (!code) return;
            word |= static_cast<std::uint32_t>(*code) << 6U;
        }
        if (!finish()) return;
        break;
    case Syntax::LoadImmediate:
    case Syntax::LoadImmediate64: {
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        cursor.skip_space();
        const auto value_location = here();
        const auto value = as.parse_absolute(cursor);
        if (!value || !finish()) return;
        if (description.syntax == Syntax::LoadImmediate64 && !available(I3)) {
            as.error(location, "dli needs 64-bit registers");
            return;
        }
        load_immediate(as, *t, *value, description.syntax == Syntax::LoadImmediate64,
                       value_location);
        return;
    }
    case Syntax::MoveMacro:
    case Syntax::Negate:
    case Syntax::NotMacro: {
        const auto d = gpr(as, cursor);
        if (!d) return;
        unsigned s = *d;
        if (cursor.consume(",")) {
            const auto source = gpr(as, cursor);
            if (!source) return;
            s = *source;
        } else if (description.syntax == Syntax::MoveMacro) {
            as.error(cursor.location(), "expected ','");
            return;
        }
        if (!finish()) return;
        // move: or d,s,$zero; negu: subu d,$zero,s; not: nor d,s,$zero.
        word |= rd(*d) | (description.syntax == Syntax::Negate ? rt(s) : rs(s));
        use_gpr(*d);
        use_gpr(s);
        use_gpr(0);
        break;
    }
    }
    emit(as, word, fixup);
    if (branch && options_.reorder) {
        use_gpr(0);
        emit(as, 0);
    }
}

void MipsTarget::set_directive(Assembler& as, Statement& statement) {
    auto& cursor = statement.operands;
    auto probe = cursor;
    const auto word = probe.identifier();
    if (!word) {
        as.error(statement.location, "expected a .set option");
        return;
    }
    const auto option = lower(*word);
    probe.skip_space();
    if (probe.peek() == ',') {
        as.assignment_statement(statement);
        return;
    }
    cursor = probe;
    if (option == "at" && cursor.consume("=")) {
        const auto number = gpr(as, cursor);
        if (!number) return;
        if (*number != 1) {
            as.error(statement.location, "only $1 can be the assembler temporary");
            return;
        }
        options_.at = true;
    } else if (option == "push") {
        stack_.push_back(options_);
    } else if (option == "pop") {
        if (stack_.empty()) {
            as.error(statement.location, ".set pop without a matching .set push");
            return;
        }
        options_ = stack_.back();
        stack_.pop_back();
    } else if (option == "reorder") {
        options_.reorder = true;
    } else if (option == "noreorder") {
        options_.reorder = false;
        noreorder_ = true;
    } else if (option == "at") {
        options_.at = true;
    } else if (option == "noat") {
        options_.at = false;
    } else if (option == "macro") {
        options_.macro = true;
    } else if (option == "nomacro") {
        options_.macro = false;
    } else if (option == "mips0") {
        options_.isa = model_.isa;
    } else if (option == "nomips16" || option == "nomicromips" || option == "volatile" ||
               option == "novolatile") {
        // Cross never enters a compressed ISA mode; volatile has no effect.
    } else {
        const auto level = std::find_if(isa_levels.begin(), isa_levels.end(),
                                        [&](const IsaLevel& entry) { return entry.name == option; });
        if (level == isa_levels.end()) {
            as.error(statement.location, "unsupported .set option '" + option + "'");
            return;
        }
        options_.isa = level->bit;
    }
    if (!cursor.at_end()) as.error(cursor.location(), "unexpected text after .set option");
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
    const auto end = as.temporary_label();
    as.symbol(*symbol).size = as.binary(assembler::Operator::Subtract,
                                        as.symbol_reference(end, statement.location),
                                        as.symbol_reference(*symbol, statement.location));
    procedure_ = {};
}

bool MipsTarget::directive(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    if (name == ".set") {
        set_directive(as, statement);
        return true;
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
        const auto section = name == ".rdata"
            ? as.section(".rodata", elf::sht_progbits, elf::shf_alloc)
            : as.section(name, name == ".sbss" ? elf::sht_nobits : elf::sht_progbits,
                         elf::shf_alloc | elf::shf_write);
        as.switch_section(section);
        return true;
    }
    if (name == ".cpload" || name == ".cprestore" || name == ".cpsetup" || name == ".cplocal" ||
        name == ".cpreturn" || name == ".cpadd" || name == ".gpword" || name == ".gpdword" ||
        name == ".module" || name == ".insn" || name == ".mips16" || name == ".nomips16" ||
        name == ".micromips" || name == ".nomicromips") {
        as.error(statement.location, "unsupported MIPS directive '" + name + "'");
        return true;
    }
    return false;
}

void MipsTarget::finish(Assembler& as) {
    // .text, .data, and .bss always exist and are 16-byte aligned, as in
    // GNU as on native systems and LLVM MC.
    for (const auto& [name, type, flags] :
         {std::tuple{std::string_view{".text"}, elf::sht_progbits, elf::shf_alloc | elf::shf_execinstr},
          std::tuple{std::string_view{".data"}, elf::sht_progbits, elf::shf_alloc | elf::shf_write},
          std::tuple{std::string_view{".bss"}, elf::sht_nobits, elf::shf_alloc | elf::shf_write}}) {
        auto& section = as.section(as.section(name, type, flags));
        section.alignment = std::max<std::uint64_t>(section.alignment, 16);
    }
    if (model_.elf64) {
        const auto id = as.section(".MIPS.options", sht_mips_options,
                                   elf::shf_alloc | shf_mips_nostrip, 1);
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
        const auto id = as.section(".reginfo", sht_mips_reginfo, elf::shf_alloc, 24);
        as.section(id).alignment = 4;
        as.push_section(id);
        as.emit_integer(gpr_mask_, 4);
        for (const auto mask : cpr_mask_) as.emit_integer(mask, 4);
        as.emit_integer(0, 4);
        as.pop_section({});
    }
    const auto& level = isa_level(model_.isa);
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
    as.emit_integer(model_.odd_spreg ? 1 : 0, 4);
    as.emit_integer(0, 4);
    as.pop_section({});
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
    Assembler& as, const assembler::Fixup& fixup, const assembler::Value&,
    const assembler::Symbol&, const assembler::Section* symbol_section) const {
    const bool merge = symbol_section && (symbol_section->flags & elf::shf_merge) != 0;
    switch (fixup.kind.value) {
    case 1:
        as.error(fixup.location, "MIPS has no one-byte relocation");
        return std::nullopt;
    case 2: return assembler::RelocationChoice{r_mips_16};
    case 4: return assembler::RelocationChoice{r_mips_32};
    case 8: return assembler::RelocationChoice{r_mips_64};
    case assembler::fixup::data4_pc.value: return assembler::RelocationChoice{r_mips_pc32};
    // REL HI16/LO16 pairs carry the offset across both halves, which a
    // linker must reassemble to find mergeable data; keep the symbol.
    case fixup_hi16: return assembler::RelocationChoice{r_mips_hi16, merge && !model_.elf64};
    case fixup_lo16: return assembler::RelocationChoice{r_mips_lo16, merge && !model_.elf64};
    case fixup_gprel16: return assembler::RelocationChoice{r_mips_gprel16};
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
    // linker's rule for reassembling the split addend. As LLVM MC does, an
    // unmatched HI16 moves before the LO16 with the lowest addend no less
    // than its own, preferring an unmatched one.
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
    MipsTarget target(model_for(subtarget, options.compiler_output));
    return assembler::assemble(sources, target, options, manager, diagnostics);
}

} // namespace cross::mips
