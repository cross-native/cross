// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/assembler.hpp"

#include "assembler/assembler.hpp"
#include "model/model.hpp"
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
using assembler::SymbolId;
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
    // CPU facts rather than levels: the Allegrex extension, and an
    // instruction of the listed levels that the Allegrex lacks.
    isa_allegrex = 1U << 9,
    isa_not_allegrex = 1U << 10,
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
constexpr std::uint16_t NOT_ALX = isa_not_allegrex;

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
    RdRsRt, RdRtRs, RdRtSa, RtRsSimm, RtRsUimm, RtUimm, RdRsCount, RdRs, RdRt,
    RdRsCc, RsRt, Divide, Rd, Rs, RtMemory, FtMemory, CacheMemory,
    RsRtBranch, RsBranch, BranchAlways, CcBranch, Jump, JumpRegister,
    JumpLinkRegister, FdFsFt, FdFs, FsFt, FdFsRt, FdFsCc, RtFs, RtControl,
    RtCoprocessor, NoOperands, SyncType, BreakCode, SyscallCode,
    TrapRegister, TrapImmediate, Extract, Insert,
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

// Encodings that share a spelling with a table entry on other CPUs, and
// spellings that only alias one.
constexpr Description alternate_forms[] = {
    {"clz", Syntax::RdRs, 0x00000016, ALX},
    {"clo", Syntax::RdRs, 0x00000017, ALX},
    {"madd", Syntax::RsRt, 0x0000001c, ALX},
    {"maddu", Syntax::RsRt, 0x0000001d, ALX},
    {"msub", Syntax::RsRt, 0x0000002e, ALX},
    {"msubu", Syntax::RsRt, 0x0000002f, ALX},
    {"ror", Syntax::RdRtSa, 0x00200002, I33 | ALX},
    {"rorv", Syntax::RdRtRs, 0x00000046, I33 | ALX},
};

// GNU as gives a three-register operation whose last operand is a constant
// the immediate instruction when the constant fits it, and otherwise loads
// the constant into $at. A word of 0 has no immediate instruction.
enum class ImmediateRange : std::uint8_t { Signed, Unsigned, Negated };
struct ImmediateForm {
    std::string_view mnemonic;
    std::uint32_t word;
    ImmediateRange range;
};
constexpr ImmediateForm immediate_forms[] = {
    {"add", 0x20000000, ImmediateRange::Signed},
    {"addu", 0x24000000, ImmediateRange::Signed},
    {"dadd", 0x60000000, ImmediateRange::Signed},
    {"daddu", 0x64000000, ImmediateRange::Signed},
    {"sub", 0x20000000, ImmediateRange::Negated},
    {"subu", 0x24000000, ImmediateRange::Negated},
    {"dsub", 0x60000000, ImmediateRange::Negated},
    {"dsubu", 0x64000000, ImmediateRange::Negated},
    {"and", 0x30000000, ImmediateRange::Unsigned},
    {"or", 0x34000000, ImmediateRange::Unsigned},
    {"xor", 0x38000000, ImmediateRange::Unsigned},
    {"nor", 0x34000000, ImmediateRange::Unsigned},
    {"slt", 0x28000000, ImmediateRange::Signed},
    {"sltu", 0x2c000000, ImmediateRange::Signed},
    {"mul", 0, ImmediateRange::Signed},
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

// The pipeline interlocks of a CPU: hazards that `.set reorder` need not
// separate with nops. They are the CPU model's facts, which the code
// generator also follows.
struct Interlocks {
    bool load{};      // loads, and coprocessor 0 moves
    bool transfer{};  // FPU and FPU control register moves
    bool compare{};   // an FPU compare and its condition's reader
    bool hilo{};      // a HI/LO read and a following write
};

// What `.set mipsN`, `.set arch=`, and `.module arch=` select.
struct Architecture {
    std::uint16_t isa{isa_mips1};
    bool allegrex{};
    // Odd single-precision registers exist.
    bool odd_single{};
    Interlocks interlocks{};
};

template <typename Has>
Architecture architecture_from(Has has) {
    Architecture result;
    result.isa = has(Feature::Mips64r2)   ? isa_mips64r2
                 : has(Feature::Mips64)   ? isa_mips64
                 : has(Feature::Mips32r2) ? isa_mips32r2
                 : has(Feature::Mips32)   ? isa_mips32
                 : has(Feature::Mips5)    ? isa_mips5
                 : has(Feature::Mips4)    ? isa_mips4
                 : has(Feature::Mips3)    ? isa_mips3
                 : has(Feature::Mips2)    ? isa_mips2
                                          : isa_mips1;
    result.allegrex = has(Feature::Allegrex);
    result.odd_single = has(Feature::OddSpreg);
    result.interlocks = {has(Feature::LoadInterlocks), has(Feature::FpuTransferInterlocks),
                         has(Feature::FpuCompareInterlocks), has(Feature::HiloInterlocks)};
    return result;
}

// The model CPU entry named `name`.
std::optional<Architecture> cpu_architecture(const Subtarget& subtarget, std::string_view name) {
    const auto& registry = model_registry();
    const auto* entry = registry.find_cpu(subtarget.target().architecture, name);
    if (!entry) return std::nullopt;
    const auto resolved = registry.resolve_cpu(subtarget.target(), *entry);
    return architecture_from([&](Feature feature) {
        return std::find(resolved.features.begin(), resolved.features.end(),
                         feature_name(feature)) != resolved.features.end();
    });
}

bool has_64bit_registers(std::uint16_t isa) { return (isa & I3) != 0; }

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
    model.architecture = architecture_from(has);
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
    } else if (has(Feature::Fpxx) || (has(Feature::Fp32) && doubleword_fpu_memory(subtarget))) {
        // Compiled code keeps to the FPXX subset of the paired-register
        // convention, so it links with either FPU. Without ldc1 and sdc1,
        // as on MIPS I, f64 halves travel through odd single registers and
        // the code stays FP32.
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

// The effects of an instruction word. With 32-bit FPRs, an instruction on
// doubles or longs names both registers of each pair.
Effects decode_effects(std::uint32_t word, bool fpr_pairs) {
    using namespace effect;
    const unsigned op = word >> 26U;
    const unsigned rs = (word >> 21U) & 31U;
    const unsigned rt = (word >> 16U) & 31U;
    const unsigned rd = (word >> 11U) & 31U;
    const unsigned sa = (word >> 6U) & 31U;
    const unsigned function = word & 63U;
    Effects e;
    bool wide = false;
    const auto read = [&](unsigned number) { e.gpr_read |= (1U << number) & ~1U; };
    const auto write = [&](unsigned number) { e.gpr_write |= (1U << number) & ~1U; };
    const auto fread = [&](unsigned number) { e.fpr_read |= 1U << number; };
    const auto fwrite = [&](unsigned number) { e.fpr_write |= 1U << number; };
    const auto branch = [&](bool is_likely) { e.flags |= delayed | (is_likely ? likely : 0U); };
    switch (op) {
    case 0x00:
        switch (function) {
        case 0x00: case 0x02: case 0x03: case 0x38: case 0x3a: case 0x3b:
        case 0x3c: case 0x3e: case 0x3f:
            write(rd);
            read(rt);
            break;
        case 0x01:  // movf, movt
            write(rd);
            read(rs);
            e.flags |= read_condition;
            break;
        case 0x08:  // jr
            read(rs);
            e.flags |= delayed | unconditional;
            break;
        case 0x09:  // jalr
            write(rd);
            read(rs);
            e.flags |= delayed | unconditional;
            break;
        case 0x0c: case 0x0d: case 0x0f:  // syscall, break, sync
            e.flags |= no_delay_slot;
            break;
        case 0x10: write(rd); e.flags |= read_hi; break;
        case 0x11: read(rs); e.flags |= write_hi; break;
        case 0x12: write(rd); e.flags |= read_lo; break;
        case 0x13: read(rs); e.flags |= write_lo; break;
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
        case 0x1f:
            read(rs);
            read(rt);
            e.flags |= write_hi | write_lo;
            break;
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x36:
            read(rs);
            read(rt);
            e.flags |= no_delay_slot;
            break;
        default:
            // Variable shifts, conditional moves, and three-register
            // arithmetic.
            write(rd);
            read(rs);
            read(rt);
            break;
        }
        break;
    case 0x01:
        if (rt <= 0x03) {
            read(rs);
            branch((rt & 2U) != 0);
        } else if (rt >= 0x08 && rt <= 0x0e) {
            read(rs);
            e.flags |= no_delay_slot;
        } else if (rt >= 0x10 && rt <= 0x13) {
            read(rs);
            write(31);
            branch((rt & 2U) != 0);
        }
        break;
    case 0x02: e.flags |= delayed | unconditional; break;
    case 0x03: write(31); e.flags |= delayed | unconditional; break;
    case 0x04: case 0x05: case 0x14: case 0x15:
        read(rs);
        read(rt);
        branch(op >= 0x14);
        break;
    case 0x06: case 0x07: case 0x16: case 0x17:
        read(rs);
        branch(op >= 0x14);
        break;
    case 0x10:  // coprocessor 0
        if (rs == 0x00 || rs == 0x01) {
            write(rt);
            e.flags |= load_coprocessor | coprocessor | cp0;
        } else if (rs == 0x04 || rs == 0x05) {
            read(rt);
            e.flags |= coprocessor_move | write_condition | coprocessor | cp0;
        } else if (function == 0x18) {
            e.flags |= no_delay_slot;  // eret
        }
        break;
    case 0x11:  // coprocessor 1
        switch (rs) {
        case 0x00: case 0x01: case 0x03:  // mfc1, dmfc1, mfhc1
            write(rt);
            fread(rd);
            wide = rs != 0x00;
            e.flags |= load_coprocessor;
            break;
        case 0x02:  // cfc1
            write(rt);
            e.flags |= load_coprocessor | coprocessor;
            break;
        case 0x04: case 0x05: case 0x07:  // mtc1, dmtc1, mthc1
            read(rt);
            fwrite(rd);
            wide = rs != 0x04;
            e.flags |= coprocessor_move;
            break;
        case 0x06:  // ctc1
            read(rt);
            e.flags |= coprocessor_move | write_condition;
            break;
        case 0x08:  // bc1f, bc1t, and their likely forms
            e.flags |= read_condition;
            branch((rt & 2U) != 0);
            break;
        default: {
            const unsigned fd = sa;
            const unsigned fs = rd;
            const unsigned ft = rt;
            // Doubles or longs in the source format or the result.
            wide = rs == 0x11 || rs == 0x15 || function == 0x21 || function == 0x25 ||
                   (function >= 0x08 && function <= 0x0b);
            if (function >= 0x30) {
                fread(fs);
                fread(ft);
                e.flags |= write_condition;
            } else if (function <= 0x03) {
                fwrite(fd);
                fread(fs);
                fread(ft);
            } else {
                fwrite(fd);
                fread(fs);
                if (function == 0x11) e.flags |= read_condition;
                if (function == 0x12 || function == 0x13) read(ft);
            }
            break;
        }
        }
        break;
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e:
    case 0x18: case 0x19:
        write(rt);
        read(rs);
        break;
    case 0x0f: write(rt); break;
    case 0x1c:  // SPECIAL2
        if (function == 0x02) {  // mul
            write(rd);
            read(rs);
            read(rt);
            e.flags |= write_hi | write_lo;
        } else if (function <= 0x05) {  // madd, maddu, msub, msubu
            read(rs);
            read(rt);
            e.flags |= read_hi | read_lo | write_hi | write_lo;
        } else if (function == 0x20 || function == 0x21 || function == 0x24 ||
                   function == 0x25) {
            write(rd);
            read(rs);
        } else if (function == 0x24 || function == 0x26) {
            // Allegrex mfic, mtic.
        }
        break;
    case 0x1f:  // SPECIAL3
        if (function == 0x00) {  // ext
            write(rt);
            read(rs);
        } else if (function == 0x04) {  // ins
            write(rt);
            read(rs);
            read(rt);
        } else if (function == 0x20) {  // wsbh, seb, seh, and the Allegrex forms
            write(rd);
            read(rt);
        }
        break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
    case 0x27: case 0x1a: case 0x1b: case 0x30: case 0x34: case 0x37:
        write(rt);
        read(rs);
        e.flags |= load_memory;
        break;
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e:
    case 0x3f:
        read(rt);
        read(rs);
        break;
    case 0x38: case 0x3c:  // sc, scd
        read(rt);
        write(rt);
        read(rs);
        break;
    case 0x2f: case 0x33:  // cache, pref
        read(rs);
        break;
    case 0x31: case 0x35:  // lwc1, ldc1
        fwrite(rt);
        read(rs);
        wide = op == 0x35;
        e.flags |= load_memory | coprocessor_load;
        break;
    case 0x39: case 0x3d:  // swc1, sdc1
        fread(rt);
        read(rs);
        wide = op == 0x3d;
        break;
    default:
        break;
    }
    if (wide && fpr_pairs) {
        e.fpr_read |= e.fpr_read << 1U;
        e.fpr_write |= e.fpr_write << 1U;
    }
    return e;
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
    MipsTarget(const Model& model, const Subtarget& subtarget, unsigned optimize)
        : model_(model), subtarget_(subtarget), optimize_(optimize) {
        file_.architecture = model.architecture;
        file_.gp64 = model.gp64;
        file_.fpr = model.fpr_mode;
        file_.odd_spreg = model.odd_spreg;
        options_ = file_;
        for (std::size_t index = 0; index < std::size(descriptions); ++index) {
            mnemonics_.emplace(descriptions[index].mnemonic, &descriptions[index]);
        }
        for (const auto& form : alternate_forms) alternates_.emplace(form.mnemonic, &form);
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
    struct Emitted {
        Effects effects;
        bool fixed{true};
        bool noreorder{};
        bool placed{};
        assembler::SectionId section{};
        std::uint32_t fragment{};
        std::size_t offset{};
        bool has_fixup{};
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
    bool sixteen(Assembler& as, std::uint32_t& word, const Immediate& value, bool is_signed,
                 std::optional<PendingFixup>& fixup);
    std::optional<std::int64_t> absolute(Assembler& as, Cursor& cursor, std::int64_t low,
                                         std::int64_t high, std::string_view what);

    // Emission. `append` places an instruction as GNU as does in the
    // current reorder mode.
    void append(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup = {},
                bool unconditional = false);
    void emit_word(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup,
                   Emitted& placed);
    void emit_nops(Assembler& as, unsigned count);
    void insert_history(std::size_t first, std::size_t count, const Emitted& entry);
    void clear_history();
    void move_labels(Assembler& as);
    void start_noreorder(Assembler& as);
    void end_noreorder(Assembler& as);
    [[nodiscard]] int insns_between(const Effects& first, const Effects* second) const;
    [[nodiscard]] int nops_for(std::size_t ignore, const Emitted* history, const Effects* next) const;
    [[nodiscard]] int nops_for_sequence(std::size_t ignore, const Emitted* history,
                                        const Effects& older, const Effects& newer) const;
    [[nodiscard]] int nops_for_target(std::size_t ignore, const Emitted* history,
                                      const Effects& next) const;
    [[nodiscard]] bool can_swap(Assembler& as, const Effects& branch) const;

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
    bool available(std::uint16_t isa) const {
        if ((isa & NOT_ALX) != 0 && options_.architecture.allegrex) return false;
        return (isa & options_.architecture.isa) != 0 ||
               ((isa & ALX) != 0 && options_.architecture.allegrex);
    }
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
    std::unordered_map<std::string_view, const Description*> mnemonics_;
    std::unordered_multimap<std::string_view, const Description*> alternates_;
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

bool MipsTarget::can_swap(Assembler& as, const Effects& branch) const {
    using namespace effect;
    const auto& previous = history_[0];
    if (optimize_ < 2 || !options_.move || previous.fixed || history_[1].noreorder ||
        !labels_.empty() || !previous.placed) {
        return false;
    }
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

void MipsTarget::insert_history(std::size_t first, std::size_t count, const Emitted& entry) {
    for (std::size_t index = history_size; index-- > first;) {
        history_[index] = index >= first + count ? history_[index - count] : entry;
    }
}

void MipsTarget::clear_history() {
    tentative_.reset();
    history_.fill(Emitted{});
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
                        const std::optional<PendingFixup>& fixup, bool unconditional) {
    using namespace effect;
    started_ = true;
    Effects effects = decode_effects(word, options_.fpr != FprMode::Fp64);
    if (unconditional) effects.flags |= effect::unconditional;
    if (numeric_fpr_) {
        effects.fpr_read = 0;
        effects.fpr_write = 0;
        numeric_fpr_ = false;
    }
    if (model_.gnu) {
        gpr_mask_ |= effects.gpr_read | effects.gpr_write;
        cpr_mask_[1] |= effects.fpr_read | effects.fpr_write;
    }
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
    const bool branch = (effects.flags & delayed) != 0;
    if (!options_.noreorder && branch && (effects.flags & likely) == 0 && can_swap(as, effects)) {
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
        clear_history();
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
    // 32-bit value.
    const auto rt = static_cast<std::uint32_t>(target) << 16U;
    const auto rs_rt = (static_cast<std::uint32_t>(target) << 21U) | rt;
    const auto start = emitted_;
    const auto addiu = [&](std::int64_t immediate) {
        use_gpr(target);
        use_gpr(0);
        append(as, 0x24000000U | rt | (static_cast<std::uint32_t>(immediate) & 0xffffU));
    };
    const auto ori_zero = [&](std::uint64_t immediate) {
        use_gpr(target);
        use_gpr(0);
        append(as, 0x34000000U | rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto ori = [&](std::uint64_t immediate) {
        use_gpr(target);
        append(as, 0x34000000U | rs_rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto lui = [&](std::uint64_t immediate) {
        use_gpr(target);
        append(as, 0x3c000000U | rt | static_cast<std::uint32_t>(immediate & 0xffffU));
    };
    const auto shift = [&](std::uint32_t function, unsigned amount) {
        use_gpr(target);
        append(as, function | rt | (static_cast<std::uint32_t>(target) << 11U) |
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
    if (warn && !options_.macro && emitted_ - start > 1) {
        as.warning(location, "macro instruction expanded into multiple instructions");
    }
}

void MipsTarget::instruction(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    const auto location = statement.location;
    numeric_fpr_ = false;
    // The table entry, else an encoding of the same spelling the current
    // instruction set has.
    const Description* chosen = nullptr;
    if (const auto found = mnemonics_.find(name); found != mnemonics_.end()) {
        chosen = found->second;
    }
    if (!chosen || !available(chosen->isa)) {
        const auto [first, last] = alternates_.equal_range(name);
        for (auto each = first; each != last; ++each) {
            if (available(each->second->isa)) {
                chosen = each->second;
                break;
            }
            if (!chosen) chosen = each->second;
        }
    }
    if (!chosen) {
        as.error(location, "unknown instruction '" + std::string(statement.name) + "'");
        return;
    }
    const auto rs = [](unsigned number) { return static_cast<std::uint32_t>(number) << 21U; };
    const auto rt = [](unsigned number) { return static_cast<std::uint32_t>(number) << 16U; };
    const auto rd = [](unsigned number) { return static_cast<std::uint32_t>(number) << 11U; };
    const auto sa = [](unsigned number) { return static_cast<std::uint32_t>(number) << 6U; };
    // The third operand of a register operation is a register, rather than
    // a constant or shift amount.
    const auto third_is_register = [&]() {
        auto probe = cursor;
        for (int commas = 0; commas < 2; ++commas) {
            const auto rest = probe.rest();
            const auto next = rest.find(',');
            if (next == std::string_view::npos) return false;
            probe.set_position(probe.position() + next + 1);
        }
        probe.skip_space();
        return probe.peek() == '$';
    };
    const auto second_is_register = [&]() {
        auto probe = cursor;
        const auto next = probe.rest().find(',');
        if (next == std::string_view::npos) return true;
        probe.set_position(probe.position() + next + 1);
        probe.skip_space();
        return probe.peek() == '$';
    };
    const auto immediate_form = std::find_if(std::begin(immediate_forms), std::end(immediate_forms),
                                             [&](const ImmediateForm& form) {
                                                 return form.mnemonic == name;
                                             });
    const bool macro_multiply = name == "mul" && !available(chosen->isa);
    if (!macro_multiply && !available(chosen->isa)) {
        as.error(location, "instruction '" + name + "' is not available in " +
                               std::string(isa_level(options_.architecture.isa).name));
        return;
    }
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

    // Three-register operations with a constant last operand.
    if (immediate_form != std::end(immediate_forms) &&
        (chosen->syntax == Syntax::RdRsRt || macro_multiply) && !third_is_register()) {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto value_location = here();
        const auto value = as.parse_absolute(cursor);
        if (!value || !finish()) return;
        std::int64_t encoded = immediate_form->range == ImmediateRange::Negated ? -*value : *value;
        const bool fits = immediate_form->word != 0 &&
                          (immediate_form->range == ImmediateRange::Unsigned ? is_uint16(encoded)
                                                                             : is_int16(encoded));
        const auto word_of = [&](std::uint32_t base) {
            return base | rt(*d) | rs(*s) | (static_cast<std::uint32_t>(encoded) & 0xffffU);
        };
        if (fits) {
            append(as, word_of(immediate_form->word));
            // `nor` with an immediate: ori, then nor with $zero.
            if (name == "nor") append(as, 0x00000027U | rd(*d) | rs(*d));
            return;
        }
        if (!use_at(as, location)) return;
        // The doubleword operations load a 64-bit constant; logical
        // operations and comparisons one as wide as the GPRs.
        const bool logical = immediate_form->range == ImmediateRange::Unsigned ||
                             name.starts_with("slt");
        load_immediate(as, 1, *value, name.starts_with("d") || (logical && options_.gp64),
                       value_location, false);
        if (name == "mul") {
            append(as, 0x00000018U | rs(*s) | rt(1));  // mult
            append(as, 0x00000012U | rd(*d));          // mflo
            return;
        }
        append(as, chosen->word | rd(*d) | rs(*s) | rt(1));
        return;
    }
    // `mul` without the MIPS32 instruction: multu and mflo.
    if (macro_multiply) {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto t = gpr(as, cursor);
        if (!t || !finish()) return;
        if (!options_.macro) {
            as.warning(location, "macro instruction expanded into multiple instructions");
        }
        append(as, 0x00000019U | rs(*s) | rt(*t));
        append(as, 0x00000012U | rd(*d));
        return;
    }
    // Traps with a constant: the immediate form, else through $at.
    if (chosen->syntax == Syntax::TrapRegister && !second_is_register()) {
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto value_location = here();
        const auto value = as.parse_absolute(cursor);
        if (!value || !finish()) return;
        static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> immediate_traps{{
            {0x34, 0x0c}, {0x36, 0x0e}, {0x30, 0x08}, {0x31, 0x09}, {0x32, 0x0a}, {0x33, 0x0b}}};
        const auto function = chosen->word & 0x3fU;
        const auto found = std::find_if(immediate_traps.begin(), immediate_traps.end(),
                                        [&](const auto& entry) { return entry.first == function; });
        if (is_int16(*value)) {
            append(as, 0x04000000U | rs(*s) | rt(found->second) |
                           (static_cast<std::uint32_t>(*value) & 0xffffU));
            return;
        }
        if (!use_at(as, location)) return;
        load_immediate(as, 1, *value, options_.gp64, value_location, false);
        append(as, chosen->word | rs(*s) | rt(1));
        return;
    }
    // Shifts by a register amount.
    Description description = *chosen;
    if (chosen->syntax == Syntax::RdRtSa && third_is_register()) {
        static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 7> variable{{
            {0x00000000, 0x00000004}, {0x00000002, 0x00000006}, {0x00000003, 0x00000007},
            {0x00000038, 0x00000014}, {0x0000003a, 0x00000016}, {0x0000003b, 0x00000017},
            {0x00200002, 0x00000046}}};
        const auto found = std::find_if(variable.begin(), variable.end(),
                                        [&](const auto& entry) { return entry.first == chosen->word; });
        if (found != variable.end()) {
            description.syntax = Syntax::RdRtRs;
            description.word = found->second;
        }
    }
    // With 32-bit GPRs GNU as expands ld and sd into word pairs.
    if (!options_.gp64 && (name == "ld" || name == "sd")) {
        as.error(location, "'" + name + "' with 32-bit GPRs is a two-word macro that is not "
                               "supported; select 64-bit GPRs with \".set gp=64\" or "
                               "\".set mips3\"");
        return;
    }

    std::uint32_t word = description.word;
    std::optional<PendingFixup> fixup;
    bool unconditional = false;
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
    case Syntax::RdRs: {
        const auto d = gpr(as, cursor);
        if (!d || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !finish()) return;
        word |= rd(*d) | rs(*s);
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
    case Syntax::TrapRegister: {
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto t = gpr(as, cursor);
        if (!t) return;
        if (cursor.consume(",")) {
            const auto code = absolute(as, cursor, 0, 1023, "trap code");
            if (!code) return;
            word |= static_cast<std::uint32_t>(*code) << 6U;
        }
        if (!finish()) return;
        word |= rs(*s) | rt(*t);
        use_gpr(*s);
        use_gpr(*t);
        break;
    }
    case Syntax::TrapImmediate: {
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto value = absolute(as, cursor, -32768, 32767, "immediate");
        if (!value || !finish()) return;
        word |= rs(*s) | (static_cast<std::uint32_t>(*value) & 0xffffU);
        use_gpr(*s);
        break;
    }
    case Syntax::Extract:
    case Syntax::Insert: {
        // ext: lsb and size-1; ins: lsb and lsb+size-1, in the sa and rd
        // fields.
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        const auto s = gpr(as, cursor);
        if (!s || !comma(as, cursor)) return;
        const auto position = absolute(as, cursor, 0, 31, "bit position");
        if (!position || !comma(as, cursor)) return;
        const auto size_location = here();
        const auto size = absolute(as, cursor, 1, 32, "bit-field size");
        if (!size || !finish()) return;
        if (*position + *size > 32) {
            as.error(size_location, "bit field " + std::to_string(*position) + "+" +
                                        std::to_string(*size) + " does not fit in 32 bits");
            return;
        }
        const auto high = description.syntax == Syntax::Extract ? *size - 1 : *position + *size - 1;
        word |= rt(*t) | rs(*s) | rd(static_cast<unsigned>(high)) |
                sa(static_cast<unsigned>(*position));
        use_gpr(*t);
        use_gpr(*s);
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
            const auto t = fpr(as, cursor, true);
            if (!t) return;
            target = *t;
            const bool wide = name == "ldc1" || name == "sdc1";
            if (!numeric_fpr_) {
                if (wide && !check_double(as, target, register_location)) return;
                use_fpr(target, wide);
            }
        } else {
            const auto operation = absolute(as, cursor, 0, 31,
                                            name == "pref" ? "prefetch hint" : "cache operation");
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
            unconditional = true;
        }
        const auto target_location = here();
        const auto target = as.parse_expression(cursor);
        if (!target || !finish()) return;
        fixup = PendingFixup{description.syntax == Syntax::Jump ? fixup_jump26 : fixup_branch16,
                             *target, target_location};
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
        const auto s = fpr(as, cursor, true);
        if (!s || !finish()) return;
        const bool high = name == "mfhc1" || name == "mthc1";
        if (!numeric_fpr_) {
            if (high && !check_double(as, *s, s_location)) return;
            use_fpr(*s, high);
        }
        word |= rt(*t) | rd(*s);
        use_gpr(*t);
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
        if (!model_.gnu) cpr_mask_[0] |= 1U << *number;
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
        const bool wide = description.syntax == Syntax::LoadImmediate64;
        const auto t = gpr(as, cursor);
        if (!t || !comma(as, cursor)) return;
        cursor.skip_space();
        const auto value_location = here();
        const auto expression = as.parse_expression(cursor);
        if (!expression || !finish()) return;
        if (wide && !available(I3)) {
            as.error(location, "dli needs 64-bit registers");
            return;
        }
        if (const auto value = as.absolute_now(*expression)) {
            load_immediate(as, *t, *value, wide, value_location);
            return;
        }
        if (wide) {
            (void)as.parse_absolute(cursor);
            as.error(value_location, "dli needs a value known before layout");
            return;
        }
        // A value only layout settles, such as a label difference: one addiu,
        // as GNU as assumes, whose immediate must then fit.
        use_gpr(*t);
        use_gpr(0);
        append(as, 0x24000000U | rt(*t),
               PendingFixup{fixup_signed16, *expression, value_location});
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
    append(as, word, fixup, unconditional);
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
    if (!model_.gnu && level != isa_levels.end()) {
        auto result = options_.architecture;
        result.isa = level->bit;
        return result;
    }
    if (auto cpu = cpu_architecture(subtarget_, name)) {
        if (level != isa_levels.end()) {
            cpu->isa = level->bit;
            cpu->allegrex = false;
        }
        return cpu;
    }
    if (level != isa_levels.end()) {
        auto result = options_.architecture;
        result.isa = level->bit;
        result.allegrex = false;
        return result;
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
