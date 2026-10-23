// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/assembler.hpp"

#include "assembler/assembler.hpp"
#include "model/model.hpp"
#include "target/aarch64/elf.hpp"
#include "target/aarch64/instructions.hpp"
#include "target/operand_encoders.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::aarch64 {
namespace {

using assembler::Assembler;
using assembler::Cursor;
using assembler::ExpressionId;
using assembler::FixupKind;
using assembler::Statement;
using assembler::SymbolId;
namespace elf = assembler::elf;

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

std::string hex(std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

bool blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// ---------------------------------------------------------------------------
// Registers and named operand values

enum class RegisterKind : std::uint8_t { X, W, Sp, Wsp, Xzr, Wzr, B, H, S, D, Q };

struct Register {
    RegisterKind kind{};
    unsigned number{};
    friend bool operator==(Register, Register) = default;
};

// The register `text` names, in any case: x0-x30, w0-w30, sp, wsp, xzr,
// wzr, fp (x29), lr (x30), and b0-b31, h0-h31, s0-s31, d0-d31, q0-q31.
std::optional<Register> parse_register(std::string_view text) {
    const auto name = lower(text);
    if (name == "sp") return Register{RegisterKind::Sp, 31};
    if (name == "wsp") return Register{RegisterKind::Wsp, 31};
    if (name == "xzr") return Register{RegisterKind::Xzr, 31};
    if (name == "wzr") return Register{RegisterKind::Wzr, 31};
    if (name == "fp") return Register{RegisterKind::X, 29};
    if (name == "lr") return Register{RegisterKind::X, 30};
    if (name.size() < 2 || name.size() > 3) return std::nullopt;
    RegisterKind kind{};
    unsigned limit = 31;
    switch (name[0]) {
    case 'x': kind = RegisterKind::X; limit = 30; break;
    case 'w': kind = RegisterKind::W; limit = 30; break;
    case 'b': kind = RegisterKind::B; break;
    case 'h': kind = RegisterKind::H; break;
    case 's': kind = RegisterKind::S; break;
    case 'd': kind = RegisterKind::D; break;
    case 'q': kind = RegisterKind::Q; break;
    default: return std::nullopt;
    }
    unsigned value{};
    const auto* end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + 1, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > limit ||
        (name.size() == 3 && name[1] == '0')) {
        return std::nullopt;
    }
    return Register{kind, value};
}

bool register_fits(OperandTypeId type, RegisterKind kind) {
    using Id = OperandTypeId;
    switch (type) {
    case Id::X: return kind == RegisterKind::X || kind == RegisterKind::Xzr;
    case Id::W: return kind == RegisterKind::W || kind == RegisterKind::Wzr;
    case Id::XSp: return kind == RegisterKind::X || kind == RegisterKind::Sp;
    case Id::WSp: return kind == RegisterKind::W || kind == RegisterKind::Wsp;
    case Id::B: return kind == RegisterKind::B;
    case Id::H: return kind == RegisterKind::H;
    case Id::S: return kind == RegisterKind::S;
    case Id::D: return kind == RegisterKind::D;
    case Id::Q: return kind == RegisterKind::Q;
    default: return false;
    }
}

std::string_view register_expectation(OperandTypeId type) {
    using Id = OperandTypeId;
    switch (type) {
    case Id::X: return "a 64-bit general register, x0-x30 or xzr";
    case Id::W: return "a 32-bit general register, w0-w30 or wzr";
    case Id::XSp: return "a 64-bit general register, x0-x30 or sp";
    case Id::WSp: return "a 32-bit general register, w0-w30 or wsp";
    case Id::B: return "an 8-bit SIMD and floating-point register, b0-b31";
    case Id::H: return "a 16-bit SIMD and floating-point register, h0-h31";
    case Id::S: return "a 32-bit SIMD and floating-point register, s0-s31";
    case Id::D: return "a 64-bit SIMD and floating-point register, d0-d31";
    case Id::Q: return "a 128-bit SIMD and floating-point register, q0-q31";
    default: return "a register";
    }
}

struct Named {
    std::string_view name;
    unsigned value;
};

constexpr Named conditions[] = {
    {"eq", 0}, {"ne", 1}, {"cs", 2}, {"hs", 2}, {"cc", 3}, {"lo", 3},
    {"mi", 4}, {"pl", 5}, {"vs", 6}, {"vc", 7}, {"hi", 8}, {"ls", 9},
    {"ge", 10}, {"lt", 11}, {"gt", 12}, {"le", 13}, {"al", 14}, {"nv", 15},
};

// The DMB and DSB options, CRm.
constexpr Named barriers[] = {
    {"oshld", 1}, {"oshst", 2}, {"osh", 3}, {"nshld", 5}, {"nshst", 6}, {"nsh", 7},
    {"ishld", 9}, {"ishst", 10}, {"ish", 11}, {"ld", 13}, {"st", 14}, {"sy", 15},
};

// The PRFM operations: type:target:policy.
constexpr Named prefetches[] = {
    {"pldl1keep", 0}, {"pldl1strm", 1}, {"pldl2keep", 2}, {"pldl2strm", 3},
    {"pldl3keep", 4}, {"pldl3strm", 5}, {"pldslckeep", 6}, {"pldslcstrm", 7},
    {"plil1keep", 8}, {"plil1strm", 9}, {"plil2keep", 10}, {"plil2strm", 11},
    {"plil3keep", 12}, {"plil3strm", 13}, {"plislckeep", 14}, {"plislcstrm", 15},
    {"pstl1keep", 16}, {"pstl1strm", 17}, {"pstl2keep", 18}, {"pstl2strm", 19},
    {"pstl3keep", 20}, {"pstl3strm", 21}, {"pstslckeep", 22}, {"pstslcstrm", 23},
};

// The PSTATE fields of MSR (immediate): op1:op2.
constexpr Named pstate_fields[] = {
    {"uao", 0x03}, {"pan", 0x04}, {"spsel", 0x05}, {"ssbs", 0x19},
    {"dit", 0x1a}, {"tco", 0x1c}, {"daifset", 0x1e}, {"daifclr", 0x1f},
};

// System registers of MRS and MSR: op0:op1:CRn:CRm:op2.
constexpr unsigned sysreg(unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return op0 << 14 | op1 << 11 | crn << 7 | crm << 3 | op2;
}

constexpr Named system_registers[] = {
    {"nzcv", sysreg(3, 3, 4, 2, 0)},
    {"daif", sysreg(3, 3, 4, 2, 1)},
    {"fpcr", sysreg(3, 3, 4, 4, 0)},
    {"fpsr", sysreg(3, 3, 4, 4, 1)},
    {"dczid_el0", sysreg(3, 3, 0, 0, 7)},
    {"ctr_el0", sysreg(3, 3, 0, 0, 1)},
    {"tpidr_el0", sysreg(3, 3, 13, 0, 2)},
    {"tpidrro_el0", sysreg(3, 3, 13, 0, 3)},
    {"tpidr_el1", sysreg(3, 0, 13, 0, 4)},
    {"tpidr_el2", sysreg(3, 4, 13, 0, 2)},
    {"tpidr_el3", sysreg(3, 6, 13, 0, 2)},
    {"cntfrq_el0", sysreg(3, 3, 14, 0, 0)},
    {"cntpct_el0", sysreg(3, 3, 14, 0, 1)},
    {"cntvct_el0", sysreg(3, 3, 14, 0, 2)},
    {"cntp_tval_el0", sysreg(3, 3, 14, 2, 0)},
    {"cntp_ctl_el0", sysreg(3, 3, 14, 2, 1)},
    {"cntp_cval_el0", sysreg(3, 3, 14, 2, 2)},
    {"cntv_tval_el0", sysreg(3, 3, 14, 3, 0)},
    {"cntv_ctl_el0", sysreg(3, 3, 14, 3, 1)},
    {"cntv_cval_el0", sysreg(3, 3, 14, 3, 2)},
    {"cntkctl_el1", sysreg(3, 0, 14, 1, 0)},
    {"cnthctl_el2", sysreg(3, 4, 14, 1, 0)},
    {"cntvoff_el2", sysreg(3, 4, 14, 0, 3)},
    {"pmcr_el0", sysreg(3, 3, 9, 12, 0)},
    {"pmccntr_el0", sysreg(3, 3, 9, 13, 0)},
    {"pmuserenr_el0", sysreg(3, 3, 9, 14, 0)},
    {"rndr", sysreg(3, 3, 2, 4, 0)},
    {"rndrrs", sysreg(3, 3, 2, 4, 1)},
    {"midr_el1", sysreg(3, 0, 0, 0, 0)},
    {"mpidr_el1", sysreg(3, 0, 0, 0, 5)},
    {"revidr_el1", sysreg(3, 0, 0, 0, 6)},
    {"id_aa64pfr0_el1", sysreg(3, 0, 0, 4, 0)},
    {"id_aa64pfr1_el1", sysreg(3, 0, 0, 4, 1)},
    {"id_aa64dfr0_el1", sysreg(3, 0, 0, 5, 0)},
    {"id_aa64isar0_el1", sysreg(3, 0, 0, 6, 0)},
    {"id_aa64isar1_el1", sysreg(3, 0, 0, 6, 1)},
    {"id_aa64mmfr0_el1", sysreg(3, 0, 0, 7, 0)},
    {"id_aa64mmfr1_el1", sysreg(3, 0, 0, 7, 1)},
    {"id_aa64mmfr2_el1", sysreg(3, 0, 0, 7, 2)},
    {"currentel", sysreg(3, 0, 4, 2, 2)},
    {"spsel", sysreg(3, 0, 4, 2, 0)},
    {"sp_el0", sysreg(3, 0, 4, 1, 0)},
    {"sp_el1", sysreg(3, 4, 4, 1, 0)},
    {"sp_el2", sysreg(3, 6, 4, 1, 0)},
    {"spsr_el1", sysreg(3, 0, 4, 0, 0)},
    {"spsr_el2", sysreg(3, 4, 4, 0, 0)},
    {"spsr_el3", sysreg(3, 6, 4, 0, 0)},
    {"elr_el1", sysreg(3, 0, 4, 0, 1)},
    {"elr_el2", sysreg(3, 4, 4, 0, 1)},
    {"elr_el3", sysreg(3, 6, 4, 0, 1)},
    {"sctlr_el1", sysreg(3, 0, 1, 0, 0)},
    {"sctlr_el2", sysreg(3, 4, 1, 0, 0)},
    {"sctlr_el3", sysreg(3, 6, 1, 0, 0)},
    {"actlr_el1", sysreg(3, 0, 1, 0, 1)},
    {"cpacr_el1", sysreg(3, 0, 1, 0, 2)},
    {"hcr_el2", sysreg(3, 4, 1, 1, 0)},
    {"scr_el3", sysreg(3, 6, 1, 1, 0)},
    {"ttbr0_el1", sysreg(3, 0, 2, 0, 0)},
    {"ttbr1_el1", sysreg(3, 0, 2, 0, 1)},
    {"tcr_el1", sysreg(3, 0, 2, 0, 2)},
    {"ttbr0_el2", sysreg(3, 4, 2, 0, 0)},
    {"tcr_el2", sysreg(3, 4, 2, 0, 2)},
    {"ttbr0_el3", sysreg(3, 6, 2, 0, 0)},
    {"tcr_el3", sysreg(3, 6, 2, 0, 2)},
    {"esr_el1", sysreg(3, 0, 5, 2, 0)},
    {"esr_el2", sysreg(3, 4, 5, 2, 0)},
    {"esr_el3", sysreg(3, 6, 5, 2, 0)},
    {"far_el1", sysreg(3, 0, 6, 0, 0)},
    {"far_el2", sysreg(3, 4, 6, 0, 0)},
    {"far_el3", sysreg(3, 6, 6, 0, 0)},
    {"par_el1", sysreg(3, 0, 7, 4, 0)},
    {"mair_el1", sysreg(3, 0, 10, 2, 0)},
    {"mair_el2", sysreg(3, 4, 10, 2, 0)},
    {"mair_el3", sysreg(3, 6, 10, 2, 0)},
    {"vbar_el1", sysreg(3, 0, 12, 0, 0)},
    {"vbar_el2", sysreg(3, 4, 12, 0, 0)},
    {"vbar_el3", sysreg(3, 6, 12, 0, 0)},
    {"contextidr_el1", sysreg(3, 0, 13, 0, 1)},
    {"mdscr_el1", sysreg(2, 0, 0, 2, 2)},
    {"icc_pmr_el1", sysreg(3, 0, 4, 6, 0)},
    {"icc_iar1_el1", sysreg(3, 0, 12, 12, 0)},
    {"icc_eoir1_el1", sysreg(3, 0, 12, 12, 1)},
    {"icc_bpr1_el1", sysreg(3, 0, 12, 12, 3)},
    {"icc_ctlr_el1", sysreg(3, 0, 12, 12, 4)},
    {"icc_sre_el1", sysreg(3, 0, 12, 12, 5)},
    {"icc_igrpen1_el1", sysreg(3, 0, 12, 12, 7)},
    {"icc_dir_el1", sysreg(3, 0, 12, 11, 1)},
    {"icc_sgi1r_el1", sysreg(3, 0, 12, 11, 5)},
    {"icc_sre_el2", sysreg(3, 4, 12, 9, 5)},
    {"icc_sre_el3", sysreg(3, 6, 12, 12, 5)},
};

std::optional<unsigned> find_named(std::span<const Named> table, std::string_view text) {
    const auto name = lower(text);
    for (const auto& item : table) {
        if (item.name == name) return item.value;
    }
    return std::nullopt;
}

// The generic name of a system register, s<op0>_<op1>_c<n>_c<m>_<op2>.
std::optional<unsigned> generic_system_register(std::string_view text) {
    const auto name = lower(text);
    std::array<unsigned, 5> parts{};
    std::size_t at = 0;
    const std::string_view prefixes[] = {"s", "_", "_c", "_c", "_"};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (name.compare(at, prefixes[index].size(), prefixes[index]) != 0) return std::nullopt;
        at += prefixes[index].size();
        const auto* begin = name.data() + at;
        const auto parsed = std::from_chars(begin, name.data() + name.size(), parts[index]);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) return std::nullopt;
        at = static_cast<std::size_t>(parsed.ptr - name.data());
    }
    const auto [op0, op1, crn, crm, op2] = parts;
    if (at != name.size() || op0 < 2 || op0 > 3 || op1 > 7 || crn > 15 || crm > 15 || op2 > 7) {
        return std::nullopt;
    }
    return sysreg(op0, op1, crn, crm, op2);
}

// The shift and extend keywords of modifier operands.
constexpr Named shift_kinds[] = {{"lsl", 0}, {"lsr", 1}, {"asr", 2}, {"ror", 3}};
constexpr Named extend_kinds[] = {{"uxtb", 0}, {"uxth", 1}, {"uxtw", 2}, {"uxtx", 3},
                                  {"sxtb", 4}, {"sxth", 5}, {"sxtw", 6}, {"sxtx", 7}};

// The keyword of a modifier operand such as `lsl #3` or `uxtw`, lower case;
// empty when `text` is no modifier.
std::string modifier_keyword(std::string_view text) {
    std::size_t end = 0;
    while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end])) != 0) ++end;
    const auto keyword = lower(text.substr(0, end));
    if (end < text.size() && !blank(text[end]) && text[end] != '#') return {};
    if (find_named(shift_kinds, keyword) || find_named(extend_kinds, keyword)) return keyword;
    return {};
}

// ---------------------------------------------------------------------------
// Relocation operators and fixups

// The fields whose values fixups give, which their operand types place.
enum class Field : std::uint8_t {
    Branch26,
    Call26,
    Branch19,
    Branch14,
    Literal19,
    Adr21,
    Page21,
    Add12,
    Ldst8,
    Ldst16,
    Ldst32,
    Ldst64,
    Ldst128,
    Move16,
};

enum class Operator : std::uint8_t {
    None,
    Lo12,
    PgHi21,
    PgHi21Nc,
    Got,
    GotLo12,
    Gottprel,
    GottprelLo12,
    GottprelG1,
    GottprelG0Nc,
    TprelHi12,
    TprelLo12,
    TprelLo12Nc,
    TprelG2,
    TprelG1,
    TprelG1Nc,
    TprelG0,
    TprelG0Nc,
    DtprelHi12,
    DtprelLo12,
    DtprelLo12Nc,
    DtprelG2,
    DtprelG1,
    DtprelG1Nc,
    DtprelG0,
    DtprelG0Nc,
    Tlsdesc,
    TlsdescLo12,
    Tlsgd,
    TlsgdLo12,
    AbsG0,
    AbsG0Nc,
    AbsG1,
    AbsG1Nc,
    AbsG2,
    AbsG2Nc,
    AbsG3,
    AbsG0S,
    AbsG1S,
    AbsG2S,
};

struct OperatorInfo {
    std::string_view name;
    // The part of the field value that the operator fixes: the 16-bit
    // piece of a move, or the shift of an add.
    unsigned implied{};
    bool tls{};
    bool got{};
    // The relocation makes a movz a movz or a movn by the sign of the
    // value; the object holds the movn, as LLVM MC writes it.
    bool signed_move{};
};

// By Operator.
constexpr OperatorInfo operators[] = {
    {"", 0, false, false},
    {"lo12", 0, false, false},
    {"pg_hi21", 0, false, false},
    {"pg_hi21_nc", 0, false, false},
    {"got", 0, false, true},
    {"got_lo12", 0, false, true},
    {"gottprel", 0, true, false},
    {"gottprel_lo12", 0, true, false},
    {"gottprel_g1", 1, true, false, true},
    {"gottprel_g0_nc", 0, true, false},
    {"tprel_hi12", 1, true, false},
    {"tprel_lo12", 0, true, false},
    {"tprel_lo12_nc", 0, true, false},
    {"tprel_g2", 2, true, false, true},
    {"tprel_g1", 1, true, false, true},
    {"tprel_g1_nc", 1, true, false},
    {"tprel_g0", 0, true, false, true},
    {"tprel_g0_nc", 0, true, false},
    {"dtprel_hi12", 1, true, false},
    {"dtprel_lo12", 0, true, false},
    {"dtprel_lo12_nc", 0, true, false},
    {"dtprel_g2", 2, true, false, true},
    {"dtprel_g1", 1, true, false, true},
    {"dtprel_g1_nc", 1, true, false},
    {"dtprel_g0", 0, true, false, true},
    {"dtprel_g0_nc", 0, true, false},
    {"tlsdesc", 0, true, false},
    {"tlsdesc_lo12", 0, true, false},
    {"tlsgd", 0, true, false},
    {"tlsgd_lo12", 0, true, false},
    {"abs_g0", 0, false, false},
    {"abs_g0_nc", 0, false, false},
    {"abs_g1", 1, false, false},
    {"abs_g1_nc", 1, false, false},
    {"abs_g2", 2, false, false},
    {"abs_g2_nc", 2, false, false},
    {"abs_g3", 3, false, false},
    {"abs_g0_s", 0, false, false},
    {"abs_g1_s", 1, false, false},
    {"abs_g2_s", 2, false, false},
};

const OperatorInfo& info(Operator op) { return operators[static_cast<unsigned>(op)]; }

std::optional<Operator> find_operator(std::string_view text) {
    const auto name = lower(text);
    for (unsigned index = 1; index < std::size(operators); ++index) {
        if (operators[index].name == name) return static_cast<Operator>(index);
    }
    return std::nullopt;
}

unsigned scale_log(Field field) {
    switch (field) {
    case Field::Ldst16: return 1;
    case Field::Ldst32: return 2;
    case Field::Ldst64: return 3;
    case Field::Ldst128: return 4;
    default: return 0;
    }
}

bool ldst(Field field) { return field >= Field::Ldst8 && field <= Field::Ldst128; }

bool pc_relative(Field field) { return field <= Field::Page21; }

// The relocation of a field with an operator; 0 for a pair that has none.
std::uint32_t relocation_type(Operator op, Field field) {
    const auto by_size = [&](std::array<std::uint32_t, 5> types) {
        return types[scale_log(field)];
    };
    switch (op) {
    case Operator::None:
        switch (field) {
        case Field::Branch26: return r_aarch64_jump26;
        case Field::Call26: return r_aarch64_call26;
        case Field::Branch19: return r_aarch64_condbr19;
        case Field::Branch14: return r_aarch64_tstbr14;
        case Field::Literal19: return r_aarch64_ld_prel_lo19;
        case Field::Adr21: return r_aarch64_adr_prel_lo21;
        case Field::Page21: return r_aarch64_adr_prel_pg_hi21;
        default: return 0;
        }
    case Operator::Lo12:
        if (field == Field::Add12) return r_aarch64_add_abs_lo12_nc;
        if (ldst(field)) {
            return by_size({r_aarch64_ldst8_abs_lo12_nc, r_aarch64_ldst16_abs_lo12_nc,
                            r_aarch64_ldst32_abs_lo12_nc, r_aarch64_ldst64_abs_lo12_nc,
                            r_aarch64_ldst128_abs_lo12_nc});
        }
        return 0;
    case Operator::PgHi21: return field == Field::Page21 ? r_aarch64_adr_prel_pg_hi21 : 0;
    case Operator::PgHi21Nc: return field == Field::Page21 ? r_aarch64_adr_prel_pg_hi21_nc : 0;
    case Operator::Got: return field == Field::Page21 ? r_aarch64_adr_got_page : 0;
    case Operator::GotLo12: return field == Field::Ldst64 ? r_aarch64_ld64_got_lo12_nc : 0;
    case Operator::Gottprel:
        return field == Field::Page21 ? r_aarch64_tlsie_adr_gottprel_page21 : 0;
    case Operator::GottprelLo12:
        return field == Field::Ldst64 ? r_aarch64_tlsie_ld64_gottprel_lo12_nc : 0;
    case Operator::GottprelG1:
        return field == Field::Move16 ? r_aarch64_tlsie_movw_gottprel_g1 : 0;
    case Operator::GottprelG0Nc:
        return field == Field::Move16 ? r_aarch64_tlsie_movw_gottprel_g0_nc : 0;
    case Operator::TprelHi12:
        return field == Field::Add12 ? r_aarch64_tlsle_add_tprel_hi12 : 0;
    case Operator::TprelLo12:
        if (field == Field::Add12) return r_aarch64_tlsle_add_tprel_lo12;
        if (ldst(field)) {
            return by_size({r_aarch64_tlsle_ldst8_tprel_lo12, r_aarch64_tlsle_ldst16_tprel_lo12,
                            r_aarch64_tlsle_ldst32_tprel_lo12, r_aarch64_tlsle_ldst64_tprel_lo12,
                            r_aarch64_tlsle_ldst128_tprel_lo12});
        }
        return 0;
    case Operator::TprelLo12Nc:
        if (field == Field::Add12) return r_aarch64_tlsle_add_tprel_lo12_nc;
        if (ldst(field)) {
            return by_size({r_aarch64_tlsle_ldst8_tprel_lo12_nc,
                            r_aarch64_tlsle_ldst16_tprel_lo12_nc,
                            r_aarch64_tlsle_ldst32_tprel_lo12_nc,
                            r_aarch64_tlsle_ldst64_tprel_lo12_nc,
                            r_aarch64_tlsle_ldst128_tprel_lo12_nc});
        }
        return 0;
    case Operator::TprelG2: return field == Field::Move16 ? r_aarch64_tlsle_movw_tprel_g2 : 0;
    case Operator::TprelG1: return field == Field::Move16 ? r_aarch64_tlsle_movw_tprel_g1 : 0;
    case Operator::TprelG1Nc:
        return field == Field::Move16 ? r_aarch64_tlsle_movw_tprel_g1_nc : 0;
    case Operator::TprelG0: return field == Field::Move16 ? r_aarch64_tlsle_movw_tprel_g0 : 0;
    case Operator::TprelG0Nc:
        return field == Field::Move16 ? r_aarch64_tlsle_movw_tprel_g0_nc : 0;
    case Operator::DtprelHi12:
        return field == Field::Add12 ? r_aarch64_tlsld_add_dtprel_hi12 : 0;
    case Operator::DtprelLo12:
        if (field == Field::Add12) return r_aarch64_tlsld_add_dtprel_lo12;
        if (ldst(field)) {
            return by_size({r_aarch64_tlsld_ldst8_dtprel_lo12, r_aarch64_tlsld_ldst16_dtprel_lo12,
                            r_aarch64_tlsld_ldst32_dtprel_lo12, r_aarch64_tlsld_ldst64_dtprel_lo12,
                            r_aarch64_tlsld_ldst128_dtprel_lo12});
        }
        return 0;
    case Operator::DtprelLo12Nc:
        if (field == Field::Add12) return r_aarch64_tlsld_add_dtprel_lo12_nc;
        if (ldst(field)) {
            return by_size({r_aarch64_tlsld_ldst8_dtprel_lo12_nc,
                            r_aarch64_tlsld_ldst16_dtprel_lo12_nc,
                            r_aarch64_tlsld_ldst32_dtprel_lo12_nc,
                            r_aarch64_tlsld_ldst64_dtprel_lo12_nc,
                            r_aarch64_tlsld_ldst128_dtprel_lo12_nc});
        }
        return 0;
    case Operator::DtprelG2:
        return field == Field::Move16 ? r_aarch64_tlsld_movw_dtprel_g2 : 0;
    case Operator::DtprelG1:
        return field == Field::Move16 ? r_aarch64_tlsld_movw_dtprel_g1 : 0;
    case Operator::DtprelG1Nc:
        return field == Field::Move16 ? r_aarch64_tlsld_movw_dtprel_g1_nc : 0;
    case Operator::DtprelG0:
        return field == Field::Move16 ? r_aarch64_tlsld_movw_dtprel_g0 : 0;
    case Operator::DtprelG0Nc:
        return field == Field::Move16 ? r_aarch64_tlsld_movw_dtprel_g0_nc : 0;
    case Operator::Tlsdesc: return field == Field::Page21 ? r_aarch64_tlsdesc_adr_page21 : 0;
    case Operator::TlsdescLo12:
        if (field == Field::Add12) return r_aarch64_tlsdesc_add_lo12;
        return field == Field::Ldst64 ? r_aarch64_tlsdesc_ld64_lo12 : 0;
    case Operator::Tlsgd: return field == Field::Page21 ? r_aarch64_tlsgd_adr_page21 : 0;
    case Operator::TlsgdLo12: return field == Field::Add12 ? r_aarch64_tlsgd_add_lo12_nc : 0;
    case Operator::AbsG0: return field == Field::Move16 ? r_aarch64_movw_uabs_g0 : 0;
    case Operator::AbsG0Nc: return field == Field::Move16 ? r_aarch64_movw_uabs_g0_nc : 0;
    case Operator::AbsG1: return field == Field::Move16 ? r_aarch64_movw_uabs_g1 : 0;
    case Operator::AbsG1Nc: return field == Field::Move16 ? r_aarch64_movw_uabs_g1_nc : 0;
    case Operator::AbsG2: return field == Field::Move16 ? r_aarch64_movw_uabs_g2 : 0;
    case Operator::AbsG2Nc: return field == Field::Move16 ? r_aarch64_movw_uabs_g2_nc : 0;
    case Operator::AbsG3: return field == Field::Move16 ? r_aarch64_movw_uabs_g3 : 0;
    case Operator::AbsG0S: return field == Field::Move16 ? r_aarch64_movw_sabs_g0 : 0;
    case Operator::AbsG1S: return field == Field::Move16 ? r_aarch64_movw_sabs_g1 : 0;
    case Operator::AbsG2S: return field == Field::Move16 ? r_aarch64_movw_sabs_g2 : 0;
    }
    return 0;
}

// A fixup kind holds the operator and the field.
FixupKind fixup_kind(Operator op, Field field) {
    return {static_cast<std::uint16_t>(FixupKind::target_base +
                                       (static_cast<unsigned>(op) << 4U) +
                                       static_cast<unsigned>(field))};
}

std::pair<Operator, Field> decode(FixupKind kind) {
    const unsigned value = kind.value - FixupKind::target_base;
    return {static_cast<Operator>(value >> 4U), static_cast<Field>(value & 15U)};
}

// The operand type whose relocated bits a field's fixup fills.
OperandTypeId field_type(Field field) {
    switch (field) {
    case Field::Branch26: return OperandTypeId::Branch26;
    case Field::Call26: return OperandTypeId::Call26;
    case Field::Branch19: return OperandTypeId::Branch19;
    case Field::Branch14: return OperandTypeId::Branch14;
    case Field::Literal19: return OperandTypeId::Literal19;
    case Field::Adr21: return OperandTypeId::Adr21;
    case Field::Page21: return OperandTypeId::Page21;
    case Field::Add12: return OperandTypeId::AddImmediate;
    case Field::Move16: return OperandTypeId::Move64;
    default: return OperandTypeId::MemoryScaled;
    }
}

// The field of an operand type's relocations.
std::optional<Field> type_field(OperandTypeId type, unsigned memory_bits) {
    using Id = OperandTypeId;
    switch (type) {
    case Id::Branch26: return Field::Branch26;
    case Id::Call26: return Field::Call26;
    case Id::Branch19: return Field::Branch19;
    case Id::Branch14: return Field::Branch14;
    case Id::Literal19: return Field::Literal19;
    case Id::Adr21: return Field::Adr21;
    case Id::Page21: return Field::Page21;
    case Id::AddImmediate: return Field::Add12;
    case Id::Move32:
    case Id::Move64: return Field::Move16;
    case Id::MemoryScaled:
        switch (memory_bits) {
        case 8: return Field::Ldst8;
        case 16: return Field::Ldst16;
        case 32: return Field::Ldst32;
        case 64: return Field::Ldst64;
        default: return Field::Ldst128;
        }
    default: return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Operand text

// An operand's text in a statement: positions of the statement cursor.
struct Item {
    std::size_t begin{};
    std::size_t end{};
};

// The comma-separated items of `text` from `begin` to `end`, outside
// brackets, braces, parentheses, and quotes.
std::vector<Item> split_items(std::string_view text, std::size_t begin, std::size_t end) {
    std::vector<Item> result;
    const auto add = [&](std::size_t from, std::size_t to) {
        while (from < to && blank(text[from])) ++from;
        while (to > from && blank(text[to - 1])) --to;
        result.push_back({from, to});
    };
    std::size_t start = begin;
    int depth = 0;
    bool quoted = false;
    bool any = false;
    for (std::size_t index = begin; index < end; ++index) {
        const char c = text[index];
        if (!blank(c)) any = true;
        if (c == '"') quoted = !quoted;
        else if (quoted) continue;
        else if (c == '[' || c == '{' || c == '(') ++depth;
        else if (c == ']' || c == '}' || c == ')') --depth;
        else if (c == ',' && depth == 0) {
            add(start, index);
            start = index + 1;
        }
    }
    if (any) add(start, end);
    return result;
}

// The parts of a memory operand `[...]` or `[...]!`.
struct MemoryText {
    std::vector<Item> parts;
    bool writeback{};
};

std::optional<MemoryText> memory_text(std::string_view text, Item item) {
    if (item.end - item.begin < 2 || text[item.begin] != '[') return std::nullopt;
    auto end = item.end;
    MemoryText result;
    if (text[end - 1] == '!') {
        result.writeback = true;
        --end;
        while (end > item.begin && blank(text[end - 1])) --end;
    }
    if (text[end - 1] != ']') return std::nullopt;
    result.parts = split_items(text, item.begin + 1, end - 1);
    if (result.parts.empty()) return std::nullopt;
    return result;
}

// A fixup of an instruction word: an operator and field, and its value.
struct PendingFixup {
    FixupKind kind{};
    ExpressionId expression{};
    SourceLocation location;
};

// An operand's field value: a register's number, an immediate's encoding,
// or for memory its offset's, with its base; and a value that a fixup adds.
struct OperandValue {
    std::int64_t value{};
    std::int64_t base{};
    std::optional<PendingFixup> fixup;
    SourceLocation location;
};

// A form of a mnemonic: the entry as spelled, the entry that encodes it,
// and the features of which the CPU needs one (any) and none (excludes),
// as indices of the target's feature table.
struct Form {
    const ModelInstruction* spelled{};
    const ModelInstruction* entry{};
    std::vector<std::size_t> features;
    std::vector<std::size_t> excludes;
};

OperandTypeId type_id(const ModelOperand& operand) {
    return static_cast<OperandTypeId>(operand.type.id);
}

// The outcome of parsing operands for a form: they parsed; their text has
// a shape that the form does not take; it has a value that does not fit;
// or a syntax error, which was reported, ends the statement.
enum class Outcome : std::uint8_t { Parsed, Mismatch, Misfit, Abort };

class Aarch64Target final : public assembler::Target {
public:
    explicit Aarch64Target(const Subtarget& subtarget) : subtarget_(subtarget) {
        const auto& table = *model_registry().instruction_table(subtarget.target().architecture);
        const auto& features = subtarget.target().subtargets->features;
        const auto index_of = [&](std::string_view name) {
            for (std::size_t index = 0; index < features.size(); ++index) {
                if (features[index].name == name) return index;
            }
            return features.size();
        };
        for (const auto& [mnemonic, indices] : table.forms) {
            auto& forms = forms_[std::string(mnemonic)];
            for (const auto index : indices) {
                const auto& spelled = table.entries[index];
                const auto& entry =
                    table.entries[spelled.is_alias() ? spelled.alias_entry : index];
                Form form{&spelled, &entry, {}, {}};
                for (const auto& name : entry.features) form.features.push_back(index_of(name));
                for (const auto& name : entry.excludes) form.excludes.push_back(index_of(name));
                forms.push_back(std::move(form));
            }
        }
        enabled_.assign(features.size(), false);
        for (const auto& name : subtarget.enabled_features()) {
            const auto index = index_of(name);
            if (index < enabled_.size()) enabled_[index] = true;
        }
    }

    [[nodiscard]] assembler::ObjectInfo object() const override {
        return {true, false, em_aarch64, true};
    }
    [[nodiscard]] std::uint32_t elf_flags() const override { return 0; }
    [[nodiscard]] assembler::FrameFacts frame_facts() const override {
        // LLVM MC's CIE: DW_CFA_def_cfa sp, 0.
        assembler::FrameFacts facts;
        facts.code_alignment = 1;
        facts.data_alignment = -4;
        facts.return_address = 30;
        facts.initial_instructions = {0x0c, 31, 0};
        facts.eh_pointer_encoding = 0x1b; // pcrel sdata4
        return facts;
    }
    [[nodiscard]] bool hash_comments() const override { return false; }
    [[nodiscard]] std::optional<unsigned> dwarf_register(std::string_view name) const override {
        const auto reg = parse_register(name);
        if (!reg) return std::nullopt;
        switch (reg->kind) {
        case RegisterKind::X:
        case RegisterKind::W:
        case RegisterKind::Sp:
        case RegisterKind::Wsp: return reg->number;
        case RegisterKind::Xzr:
        case RegisterKind::Wzr: return std::nullopt;
        default: return 64 + reg->number;
        }
    }
    [[nodiscard]] assembler::FixupInfo fixup_info(FixupKind kind) const override {
        const auto field = decode(kind).second;
        return {4, pc_relative(field), field == Field::Page21};
    }
    void code_padding(std::span<std::uint8_t> bytes) const override {
        // Zeros up to a word, then NOPs, as LLVM MC pads.
        const auto zeros = bytes.size() % 4;
        std::fill(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(zeros),
                  std::uint8_t{0});
        for (auto at = zeros; at + 4 <= bytes.size(); at += 4) {
            bytes[at] = 0x1f;
            bytes[at + 1] = 0x20;
            bytes[at + 2] = 0x03;
            bytes[at + 3] = 0xd5;
        }
    }

    void instruction(Assembler& as, Statement& statement) override;
    bool directive(Assembler& as, Statement& statement) override;
    void data(Assembler& as) override;
    // Code sections are aligned to words, as in LLVM MC and GNU as, also
    // without instructions.
    void finish(Assembler& as) override {
        for (std::uint32_t index = 0; index < as.sections().size(); ++index) {
            auto& section = as.section(assembler::SectionId{index});
            if ((section.flags & elf::shf_execinstr) != 0) {
                section.alignment = std::max<std::uint64_t>(section.alignment, 4);
            }
        }
    }
    bool apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                     std::span<std::uint8_t> bytes, bool relocated) const override;
    std::optional<assembler::RelocationChoice> relocation(
        Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
        const assembler::Symbol& symbol, const assembler::Section* symbol_section) const override;

private:
    // Mapping symbols ($x before instructions, $d before data) mark the
    // code and data of sections that hold code, as GNU as does.
    enum class Mapping : std::uint8_t { None, Code, Data };
    void map(Assembler& as, Mapping state);
    void mapping_symbol(Assembler& as, std::string_view name, bool at_start);

    [[nodiscard]] bool available(const Form& form) const {
        const auto on = [&](std::size_t index) { return index < enabled_.size() && enabled_[index]; };
        return (form.features.empty() || std::any_of(form.features.begin(), form.features.end(), on)) &&
               std::none_of(form.excludes.begin(), form.excludes.end(), on);
    }
    std::string needs(const Form& form) const;

    // Parsing. With `report`, a failure is an error; without, parsing only
    // probes the form, and only a syntax error is reported (and aborts).
    Outcome parse_operands(Assembler& as, const Cursor& cursor, const std::vector<Item>& items,
                           const ModelInstruction& entry, std::vector<OperandValue>& values,
                           bool report);
    Outcome parse_operand(Assembler& as, const Cursor& cursor, const std::vector<Item>& items,
                          std::size_t& next, const ModelInstruction& entry, std::size_t index,
                          OperandValue& value, bool report);
    Outcome parse_memory(Assembler& as, const Cursor& cursor, const std::vector<Item>& items,
                         std::size_t& next, const ModelOperand& operand, OperandValue& value,
                         bool report);

    struct Immediate {
        Operator op{Operator::None};
        ExpressionId expression{};
        SourceLocation location;
        std::optional<std::int64_t> known;
    };
    std::optional<Immediate> immediate(Assembler& as, const Cursor& cursor, Item item);

    void emit(Assembler& as, const ModelInstruction& entry, const std::vector<OperandValue>& values);
    void emit_word(Assembler& as, std::uint32_t word, const std::optional<PendingFixup>& fixup);

    // Directives.
    bool select(Assembler& as, std::string_view text, SourceLocation location, bool extension);

    const Subtarget& subtarget_;
    std::unordered_map<std::string, std::vector<Form>> forms_;
    // Where in the statement the last parse of operands found a shape that
    // no operand of the form takes.
    std::size_t failure_{};
    // The features in effect, by index of the target's feature table, and
    // those that `.arch_extension noNAME` turned off.
    std::vector<bool> enabled_;
    std::unordered_set<std::string> disabled_;
    // By section.
    std::vector<Mapping> mapping_;
    std::vector<bool> early_data_;
};

std::string Aarch64Target::needs(const Form& form) const {
    std::string result;
    const auto& names = form.entry->features;
    for (std::size_t index = 0; index < names.size(); ++index) {
        result += index == 0 ? "" : index + 1 == names.size() ? " or " : ", ";
        result += "'" + names[index] + "'";
    }
    if (!result.empty()) result = "feature " + result;
    for (const auto& name : form.entry->excludes) {
        const auto& features = subtarget_.target().subtargets->features;
        for (std::size_t index = 0; index < features.size(); ++index) {
            if (features[index].name == name && enabled_[index]) {
                result += std::string(result.empty() ? "" : " and ") + "no feature '" + name + "'";
            }
        }
    }
    return result;
}

void Aarch64Target::mapping_symbol(Assembler& as, std::string_view name, bool at_start) {
    const auto id = as.temporary_label();
    auto& symbol = as.symbol(id);
    symbol.name = std::string(name);
    symbol.temporary = false;
    symbol.binding = assembler::Symbol::Binding::Local;
    if (at_start) {
        symbol.fragment = 0;
        symbol.offset = 0;
    }
}

void Aarch64Target::map(Assembler& as, Mapping state) {
    const auto section = as.current_section().value;
    if (mapping_.size() <= section) {
        mapping_.resize(section + 1, Mapping::None);
        early_data_.resize(section + 1, false);
    }
    auto& current = mapping_[section];
    if (current == state) return;
    const bool code_section = (as.section(as.current_section()).flags & elf::shf_execinstr) != 0;
    if (state == Mapping::Data) {
        // Data of a section without code needs no mapping symbol until code
        // follows it.
        if (current == Mapping::None && !code_section) {
            early_data_[section] = true;
            return;
        }
        mapping_symbol(as, "$d", false);
    } else {
        if (current == Mapping::None && early_data_[section]) mapping_symbol(as, "$d", true);
        // Instructions start on a word.
        if (current == Mapping::Data || early_data_[section]) as.emit_alignment(4, 0, 0);
        mapping_symbol(as, "$x", false);
    }
    current = state;
}

void Aarch64Target::data(Assembler& as) { map(as, Mapping::Data); }

void Aarch64Target::emit_word(Assembler& as, std::uint32_t word,
                              const std::optional<PendingFixup>& fixup) {
    map(as, Mapping::Code);
    auto& section = as.section(as.current_section());
    section.alignment = std::max<std::uint64_t>(section.alignment, 4);
    as.begin_instruction();
    if (fixup) as.add_fixup(fixup->kind, fixup->expression, fixup->location);
    as.emit_integer(word, 4);
}

std::optional<Aarch64Target::Immediate> Aarch64Target::immediate(Assembler& as,
                                                                 const Cursor& cursor, Item item) {
    auto at = cursor;
    at.set_position(item.begin);
    Immediate result;
    result.location = at.location();
    if (at.peek() == '=') {
        as.error(result.location, "literal pool loads (ldr REGISTER, =VALUE) are not supported");
        return std::nullopt;
    }
    if (at.peek() == '#') at.set_position(at.position() + 1);
    if (at.peek() == ':') {
        const auto text = at.text();
        const auto close = text.find(':', at.position() + 1);
        if (close == std::string_view::npos || close >= item.end) {
            as.error(result.location, "expected ':' after a relocation operator");
            return std::nullopt;
        }
        const auto name = text.substr(at.position() + 1, close - at.position() - 1);
        const auto op = find_operator(name);
        if (!op) {
            as.error(result.location, "unknown relocation operator ':" + std::string(name) + ":'");
            return std::nullopt;
        }
        result.op = *op;
        at.set_position(close + 1);
    }
    const auto expression = as.parse_expression(at);
    if (!expression) return std::nullopt;
    at.skip_space();
    if (at.position() < item.end) {
        as.error(at.location(), "unexpected text in an operand");
        return std::nullopt;
    }
    result.expression = *expression;
    if (result.op == Operator::None) result.known = as.absolute_now(*expression);
    return result;
}

// A floating-point constant: [+-]digits[.digits][e[+-]digits], or an
// integer.
std::optional<double> floating_constant(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    double value{};
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value, std::chars_format::general);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end ||
        !(std::isdigit(static_cast<unsigned char>(text.front())) != 0)) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

Outcome Aarch64Target::parse_operands(Assembler& as, const Cursor& cursor,
                                      const std::vector<Item>& items,
                                      const ModelInstruction& entry,
                                      std::vector<OperandValue>& values, bool report) {
    values.assign(entry.operands.size(), {});
    std::size_t next = 0;
    for (std::size_t index = 0; index < entry.operands.size(); ++index) {
        const auto& operand = entry.operands[index];
        if (next == items.size()) {
            if (!operand.default_value && !operand.default_operand) {
                failure_ = cursor.text().size();
                if (report) {
                    const auto required = std::count_if(
                        entry.operands.begin(), entry.operands.end(), [](const ModelOperand& item) {
                            return !item.default_value && !item.default_operand;
                        });
                    as.error(cursor.location(), "'" + entry.mnemonic + "' needs " +
                                                    std::to_string(required) +
                                                    (required == 1 ? " operand" : " operands"));
                }
                return Outcome::Mismatch;
            }
            for (auto rest = index; rest < entry.operands.size(); ++rest) {
                const auto& omitted = entry.operands[rest];
                if (omitted.default_operand) values[rest] = values[*omitted.default_operand];
                else values[rest].value = omitted.default_value.value_or(0);
            }
            break;
        }
        const auto outcome =
            parse_operand(as, cursor, items, next, entry, index, values[index], report);
        if (outcome != Outcome::Parsed) return outcome;
    }
    if (next < items.size()) {
        failure_ = items[next].begin;
        if (report) {
            auto at = cursor;
            at.set_position(items[next].begin);
            as.error(at.location(), "'" + entry.mnemonic + "' takes " +
                                        std::to_string(entry.operands.size()) +
                                        (entry.operands.size() == 1 ? " operand" : " operands"));
        }
        return Outcome::Mismatch;
    }
    return Outcome::Parsed;
}

Outcome Aarch64Target::parse_operand(Assembler& as, const Cursor& cursor,
                                     const std::vector<Item>& items, std::size_t& next,
                                     const ModelInstruction& entry, std::size_t index,
                                     OperandValue& value, bool report) {
    using Id = OperandTypeId;
    const auto& operand = entry.operands[index];
    const auto item = items[next];
    const auto source = cursor.text();
    const auto text = source.substr(item.begin, item.end - item.begin);
    auto at = cursor;
    at.set_position(item.begin);
    value.location = at.location();
    const auto mismatch = [&](std::string message) {
        failure_ = item.begin;
        if (report) as.error(value.location, message);
        return Outcome::Mismatch;
    };
    const auto misfit = [&](SourceLocation location, std::string message) {
        if (report) as.error(location, message);
        return Outcome::Misfit;
    };
    const auto parsed = [&](std::int64_t field, std::size_t consumed = 1) {
        value.value = field;
        next += consumed;
        return Outcome::Parsed;
    };
    const auto reg = parse_register(text);
    // A literal register of the form.
    if (operand.name.empty()) {
        if (!reg || reg != parse_register(operand.type_name)) {
            return mismatch("'" + entry.mnemonic + "' takes '" + operand.type_name +
                            "' as operand " + std::to_string(index + 1));
        }
        return parsed(reg->number);
    }
    const auto id = type_id(operand);
    // The modifier item after an immediate, such as `lsl #12`.
    const auto shift_after = [&]() -> std::optional<Item> {
        if (next + 1 < items.size() &&
            modifier_keyword(source.substr(items[next + 1].begin,
                                           items[next + 1].end - items[next + 1].begin)) == "lsl") {
            return items[next + 1];
        }
        return std::nullopt;
    };
    // The amount of a modifier item, if it has one: Parsed, Misfit when it
    // is missing but `required` or not constant, Abort after a syntax error.
    const auto modifier_amount = [&](Item modifier, bool required,
                                     std::optional<std::int64_t>& amount) -> Outcome {
        auto cursor_at = cursor;
        cursor_at.set_position(modifier.begin);
        (void)cursor_at.identifier();
        cursor_at.skip_space();
        if (cursor_at.position() >= modifier.end) {
            if (!required) return Outcome::Parsed;
            if (report) as.error(cursor_at.location(), "expected a shift amount");
            return Outcome::Misfit;
        }
        if (cursor_at.peek() == '#') cursor_at.set_position(cursor_at.position() + 1);
        const auto location = cursor_at.location();
        const auto parsed_amount = as.parse_expression(cursor_at);
        if (!parsed_amount) return Outcome::Abort;
        cursor_at.skip_space();
        const auto known = as.absolute_now(*parsed_amount);
        if (cursor_at.position() < modifier.end || !known) {
            if (report) as.error(location, "expected a constant shift amount");
            return Outcome::Misfit;
        }
        amount = *known;
        return Outcome::Parsed;
    };
    switch (id) {
    case Id::X:
    case Id::W:
    case Id::XSp:
    case Id::WSp:
    case Id::B:
    case Id::H:
    case Id::S:
    case Id::D:
    case Id::Q:
        if (!reg || !register_fits(id, reg->kind)) {
            return mismatch("expected " + std::string(register_expectation(id)));
        }
        return parsed(reg->number);
    case Id::Condition:
    case Id::InvertedCondition: {
        const auto condition = find_named(conditions, text);
        if (!condition || (id == Id::InvertedCondition && *condition >= 14)) {
            return mismatch(id == Id::InvertedCondition
                                ? "expected a condition code other than al and nv"
                                : "expected a condition code such as eq or lt");
        }
        return parsed(id == Id::InvertedCondition ? (*condition ^ 1U) : *condition);
    }
    case Id::SystemRegister: {
        const auto found = find_named(system_registers, text);
        const auto number = found ? found : generic_system_register(text);
        if (!number) {
            if (reg || text.empty() || text.front() == '#' || text.front() == '[') {
                return mismatch("expected a system register");
            }
            return misfit(value.location, "unknown system register '" + std::string(text) + "'");
        }
        return parsed(*number);
    }
    case Id::PstateField: {
        const auto number = find_named(pstate_fields, text);
        if (!number) return mismatch("expected a PSTATE field such as daifset");
        return parsed(*number);
    }
    case Id::Barrier:
    case Id::Prefetch: {
        const auto& table = id == Id::Barrier ? std::span<const Named>(barriers)
                                              : std::span<const Named>(prefetches);
        if (const auto number = find_named(table, text)) return parsed(*number);
        if (reg || text.empty() || std::isalpha(static_cast<unsigned char>(text.front())) != 0 ||
            text.front() == '[') {
            return mismatch(id == Id::Barrier ? "expected a barrier option such as ish"
                                              : "expected a prefetch operation such as pldl1keep");
        }
        const auto parsed_value = immediate(as, cursor, item);
        if (!parsed_value) return Outcome::Abort;
        if (!parsed_value->known || *parsed_value->known < 0 ||
            *parsed_value->known > operand.type.maximum) {
            return misfit(value.location, "immediate is out of range [0, " +
                                              std::to_string(operand.type.maximum) + "]");
        }
        return parsed(*parsed_value->known);
    }
    case Id::FpZero:
    case Id::Fp8: {
        if (reg || text.empty() || text.front() == '[') {
            return mismatch("expected a floating-point constant");
        }
        const auto constant = floating_constant(text);
        if (!constant) return mismatch("expected a floating-point constant");
        if (id == Id::FpZero) {
            if (*constant != 0.0 || std::signbit(*constant)) return mismatch("expected #0.0");
            return parsed(0);
        }
        const auto encoded = encoders::fp8(*constant);
        if (!encoded) {
            return misfit(value.location, "floating-point constant " + std::string(text) +
                                              " has no 8-bit encoding");
        }
        return parsed(*encoded);
    }
    case Id::Shift32:
    case Id::Shift64:
    case Id::ShiftRotate32:
    case Id::ShiftRotate64:
    case Id::Extend32:
    case Id::Extend64W:
    case Id::Extend64X: {
        const auto keyword = modifier_keyword(text);
        const bool shift = id == Id::Shift32 || id == Id::Shift64 || id == Id::ShiftRotate32 ||
                           id == Id::ShiftRotate64;
        std::optional<unsigned> kind;
        if (shift) {
            kind = find_named(shift_kinds, keyword);
            if (kind && *kind == 3 && id != Id::ShiftRotate32 && id != Id::ShiftRotate64) kind.reset();
        } else if (keyword == "lsl") {
            if (id != Id::Extend64W) kind = id == Id::Extend32 ? 2U : 3U;
        } else {
            kind = find_named(extend_kinds, keyword);
            const bool wide = kind && (*kind == 3 || *kind == 7);
            if (kind && id == Id::Extend64W && wide) kind.reset();
            if (kind && id == Id::Extend64X && !wide) kind.reset();
        }
        if (!kind) {
            switch (id) {
            case Id::Shift32:
            case Id::Shift64: return mismatch("expected lsl, lsr, or asr");
            case Id::ShiftRotate32:
            case Id::ShiftRotate64: return mismatch("expected lsl, lsr, asr, or ror");
            case Id::Extend64W: return mismatch("expected uxtb, uxth, uxtw, sxtb, sxth, or sxtw");
            case Id::Extend64X: return mismatch("expected uxtx, sxtx, or lsl");
            default: return mismatch("expected an extend such as uxtw or sxtx");
            }
        }
        std::optional<std::int64_t> amount;
        if (const auto outcome = modifier_amount(item, shift, amount);
            outcome != Outcome::Parsed) {
            return outcome;
        }
        const std::int64_t limit = !shift ? 4
                                   : id == Id::Shift32 || id == Id::ShiftRotate32 ? 31
                                                                                   : 63;
        const auto amount_value = amount.value_or(0);
        if (amount_value < 0 || amount_value > limit) {
            return misfit(value.location, std::string(shift ? "shift" : "extend") + " amount " +
                                              std::to_string(amount_value) +
                                              " is out of range [0, " + std::to_string(limit) + "]");
        }
        return parsed(shift ? (*kind << 6U) | amount_value : (*kind << 3U) | amount_value);
    }
    case Id::Memory:
    case Id::MemoryScaled:
    case Id::MemoryUnscaled:
    case Id::MemoryPre:
    case Id::MemoryPost:
    case Id::MemoryRegister:
    case Id::Pair:
    case Id::PairPre:
    case Id::PairPost:
        return parse_memory(as, cursor, items, next, operand, value, report);
    default:
        break;
    }
    // Immediates and labels.
    if (reg || text.empty() || text.front() == '[' || !modifier_keyword(text).empty() ||
        find_named(conditions, text)) {
        return mismatch(operand.type.kind == OperandKind::Label ? "expected a label"
                                                                : "expected an immediate");
    }
    const auto parsed_value = immediate(as, cursor, item);
    if (!parsed_value) return Outcome::Abort;
    const auto& number = parsed_value->known;
    const auto op = parsed_value->op;
    const auto field =
        type_field(id, operand.type.memory_bits);
    if (op != Operator::None && (!field || relocation_type(op, *field) == 0)) {
        return misfit(value.location, "relocation operator ':" +
                                          std::string(info(op).name) +
                                          ":' does not apply to this operand");
    }
    const auto with_fixup = [&](std::int64_t field_value, std::size_t consumed = 1) {
        value.fixup = PendingFixup{fixup_kind(op, *field), parsed_value->expression,
                                   value.location};
        return parsed(field_value, consumed);
    };
    const auto out_of_range = [&](std::int64_t minimum, std::int64_t maximum) {
        return misfit(value.location, "immediate " + std::to_string(*number) +
                                          " is out of range [" + std::to_string(minimum) + ", " +
                                          std::to_string(maximum) + "]");
    };
    const auto needs_constant = [&]() {
        return misfit(value.location, "the immediate must be known before layout");
    };
    switch (id) {
    case Id::Immediate:
        if (!number) return needs_constant();
        if (*number < operand.type.minimum || *number > operand.type.maximum) {
            return out_of_range(operand.type.minimum, operand.type.maximum);
        }
        return parsed(*number);
    case Id::Bitmask32:
    case Id::Bitmask64: {
        if (!number) return needs_constant();
        const unsigned bits = id == Id::Bitmask32 ? 32 : 64;
        auto pattern = static_cast<std::uint64_t>(*number);
        std::optional<std::uint32_t> encoded;
        if (bits == 64 || (*number >= -(std::int64_t{1} << 31) && *number < (std::int64_t{1} << 32))) {
            if (bits == 32) pattern &= 0xffffffffU;
            encoded = encoders::bitmask(pattern, bits);
        }
        if (!encoded) {
            return misfit(value.location, "immediate " + hex(static_cast<std::uint64_t>(*number)) +
                                              " is not a " + std::to_string(bits) +
                                              "-bit logical immediate");
        }
        return parsed(*encoded);
    }
    case Id::Wide32:
    case Id::Wide64:
    case Id::WideInverted32:
    case Id::WideInverted64: {
        if (!number) return needs_constant();
        const unsigned bits = id == Id::Wide32 || id == Id::WideInverted32 ? 32 : 64;
        auto pattern = static_cast<std::uint64_t>(*number);
        std::optional<std::uint32_t> encoded;
        if (bits == 64 || (*number >= -(std::int64_t{1} << 31) && *number < (std::int64_t{1} << 32))) {
            if (bits == 32) pattern &= 0xffffffffU;
            if (id == Id::WideInverted32 || id == Id::WideInverted64) {
                pattern = ~pattern & (bits == 64 ? ~std::uint64_t{0} : 0xffffffffU);
            }
            encoded = encoders::wide(pattern, bits);
        }
        if (!encoded) {
            return misfit(value.location, "immediate " + hex(static_cast<std::uint64_t>(*number)) +
                                              " has no single-instruction move encoding");
        }
        return parsed(*encoded);
    }
    case Id::AddImmediate:
    case Id::AddImmediateNegated: {
        const auto modifier = shift_after();
        std::optional<std::int64_t> amount;
        if (modifier) {
            if (const auto outcome = modifier_amount(*modifier, true, amount);
                outcome != Outcome::Parsed) {
                return outcome;
            }
            if (*amount != 0 && *amount != 12) {
                auto location = cursor;
                location.set_position(modifier->begin);
                return misfit(location.location(), "the shift of an add immediate is lsl #0 or "
                                                   "lsl #12");
            }
        }
        const std::size_t consumed = modifier ? 2 : 1;
        const unsigned shift = amount.value_or(0) == 12 ? 1 : 0;
        if (id == Id::AddImmediateNegated) {
            // A negative value, which the opposite operation takes negated.
            if (!number) return needs_constant();
            const auto magnitude = -*number;
            const auto encoded = *number >= 0 ? std::nullopt
                                 : modifier ? (magnitude <= 4095
                                                   ? std::optional<std::uint32_t>(
                                                         static_cast<std::uint32_t>(
                                                             (shift << 12) | magnitude))
                                                   : std::nullopt)
                                            : encoders::shifted(
                                                  static_cast<std::uint64_t>(magnitude), 12, 12);
            if (!encoded) {
                return misfit(value.location, "immediate " + std::to_string(*number) +
                                                  " is not a negated 12-bit value, optionally "
                                                  "shifted left by 12");
            }
            return parsed(*encoded, consumed);
        }
        if (op != Operator::None) {
            if (shift && info(op).implied == 0) {
                return misfit(value.location, "relocation operator ':" +
                                                  std::string(info(op).name) +
                                                  ":' takes no lsl #12");
            }
            return with_fixup(std::int64_t{shift | info(op).implied} << 12, consumed);
        }
        if (!number) return with_fixup(std::int64_t{shift} << 12, consumed);
        if (modifier) {
            if (*number < 0 || *number > 4095) return out_of_range(0, 4095);
            return parsed((std::int64_t{shift} << 12) | *number, consumed);
        }
        const auto encoded = *number < 0 ? std::nullopt
                                         : encoders::shifted(static_cast<std::uint64_t>(*number),
                                                             12, 12);
        if (!encoded) {
            return misfit(value.location, "immediate " + std::to_string(*number) +
                                              " is not a 12-bit unsigned value, optionally "
                                              "shifted left by 12");
        }
        return parsed(*encoded, consumed);
    }
    case Id::Move32:
    case Id::Move64: {
        const unsigned pieces = id == Id::Move32 ? 2 : 4;
        const auto modifier = shift_after();
        std::optional<std::int64_t> amount;
        if (modifier) {
            if (const auto outcome = modifier_amount(*modifier, true, amount);
                outcome != Outcome::Parsed) {
                return outcome;
            }
            if (*amount < 0 || *amount % 16 != 0 || *amount / 16 >= pieces) {
                auto location = cursor;
                location.set_position(modifier->begin);
                return misfit(location.location(),
                              pieces == 2 ? "the shift of a 32-bit move is lsl #0 or lsl #16"
                                          : "the shift of a 64-bit move is lsl #0, #16, #32, or #48");
            }
            if (op != Operator::None) {
                auto location = cursor;
                location.set_position(modifier->begin);
                return misfit(location.location(), "a relocation operator gives the shift");
            }
        }
        const std::size_t consumed = modifier ? 2 : 1;
        const std::int64_t piece = amount.value_or(0) / 16;
        if (op != Operator::None) {
            if (info(op).implied >= pieces) {
                return misfit(value.location, "relocation operator ':" +
                                                  std::string(info(op).name) +
                                                  ":' needs a 64-bit register");
            }
            return with_fixup(std::int64_t{info(op).implied} << 16, consumed);
        }
        if (!number) return with_fixup(piece << 16, consumed);
        if (*number < 0 || *number > 0xffff) return out_of_range(0, 0xffff);
        return parsed((piece << 16) | *number, consumed);
    }
    case Id::Branch26:
    case Id::Call26:
    case Id::Branch19:
    case Id::Branch14:
    case Id::Literal19:
    case Id::Adr21:
    case Id::Page21: {
        if (op != Operator::None || !number) return with_fixup(0);
        // A constant is an offset from the instruction.
        const std::int64_t unit = id == Id::Page21 ? 4096 : id == Id::Adr21 ? 1 : 4;
        const auto limit = operand.type.maximum;
        if (*number % unit != 0 || *number / unit < operand.type.minimum || *number / unit > limit) {
            return misfit(value.location, "offset " + std::to_string(*number) +
                                              " is not a multiple of " + std::to_string(unit) +
                                              " in [" + std::to_string(operand.type.minimum * unit) +
                                              ", " + std::to_string(limit * unit) + "]");
        }
        return parsed(*number / unit);
    }
    default:
        return mismatch("unsupported operand type '" + operand.type_name + "'");
    }
}

Outcome Aarch64Target::parse_memory(Assembler& as, const Cursor& cursor,
                                    const std::vector<Item>& items, std::size_t& next,
                                    const ModelOperand& operand, OperandValue& value,
                                    bool report) {
    using Id = OperandTypeId;
    const auto source = cursor.text();
    const auto item = items[next];
    const auto id = type_id(operand);
    const auto location_of = [&](std::size_t position) {
        auto at = cursor;
        at.set_position(position);
        return at.location();
    };
    // A shape that the operand does not take, found at `position`.
    const auto mismatch = [&](std::string message, std::size_t position = 0) {
        failure_ = std::max(position, item.begin);
        if (report) as.error(value.location, message);
        return Outcome::Mismatch;
    };
    const auto misfit = [&](SourceLocation location, std::string message) {
        if (report) as.error(location, message);
        return Outcome::Misfit;
    };
    const auto memory = memory_text(source, item);
    if (!memory) return mismatch("expected a memory operand such as [x0, #8]");
    const auto& parts = memory->parts;
    const auto text_of = [&](Item part) { return source.substr(part.begin, part.end - part.begin); };
    const auto base = parse_register(text_of(parts[0]));
    if (!base || !register_fits(Id::XSp, base->kind)) {
        return mismatch("expected x0-x30 or sp as the base register");
    }
    value.base = base->number;
    const bool pre = id == Id::MemoryPre || id == Id::PairPre;
    const bool post = id == Id::MemoryPost || id == Id::PairPost;
    if (memory->writeback != pre) {
        return mismatch(pre ? "expected a pre-indexed memory operand such as [x0, #8]!"
                            : "unexpected '!' after the memory operand");
    }
    std::size_t consumed = 1;
    // The offset: inside the brackets, or after them for post-indexing.
    std::optional<Item> offset;
    if (post) {
        if (parts.size() != 1 || next + 1 >= items.size()) {
            return mismatch("expected a post-indexed memory operand such as [x0], #8");
        }
        offset = items[next + 1];
        consumed = 2;
    } else if (parts.size() >= 2) {
        offset = parts[1];
    }
    if (id == Id::MemoryRegister) {
        if (parts.size() < 2 || parts.size() > 3) {
            return mismatch("expected a register offset such as [x0, x1, lsl #3]");
        }
        const auto index = parse_register(text_of(parts[1]));
        const bool x = index && register_fits(Id::X, index->kind);
        const bool w = index && register_fits(Id::W, index->kind);
        if (!x && !w) return mismatch("expected an index register such as [x0, x1]", parts[1].begin);
        unsigned option = 3;  // lsl
        std::optional<std::int64_t> amount;
        if (parts.size() == 3) {
            const auto keyword = modifier_keyword(text_of(parts[2]));
            const auto known = keyword == "lsl" ? std::optional<unsigned>(3)
                                                : find_named(extend_kinds, keyword);
            if (!known || (x && *known != 3 && *known != 7) ||
                (w && *known != 2 && *known != 6)) {
                return mismatch(x ? "expected lsl or sxtx after a 64-bit index register"
                                  : "expected uxtw or sxtw after a 32-bit index register",
                                parts[2].begin);
            }
            option = *known;
            auto at = cursor;
            at.set_position(parts[2].begin);
            (void)at.identifier();
            at.skip_space();
            if (at.position() < parts[2].end) {
                if (at.peek() == '#') at.set_position(at.position() + 1);
                const auto location = at.location();
                const auto expression = as.parse_expression(at);
                if (!expression) return Outcome::Abort;
                at.skip_space();
                amount = as.absolute_now(*expression);
                if (at.position() < parts[2].end || !amount) {
                    return misfit(location, "expected a constant shift amount");
                }
            } else if (keyword == "lsl") {
                return mismatch("expected a shift amount after lsl", parts[2].end);
            }
        } else if (w) {
            return mismatch("expected uxtw or sxtw after a 32-bit index register",
                            parts[1].end);
        }
        const auto log = static_cast<std::int64_t>(std::countr_zero(operand.type.memory_bits / 8));
        if (amount && *amount != 0 && *amount != log) {
            return misfit(location_of(parts[2].begin),
                          "the shift amount is " +
                              (log == 0 ? std::string("0") : "0 or " + std::to_string(log)));
        }
        const unsigned scaled = amount && *amount == log ? 1 : 0;
        value.value = static_cast<std::int64_t>(index->number << 4U | option << 1U | scaled);
        next += consumed;
        return Outcome::Parsed;
    }
    if (parts.size() > 2) return mismatch("expected a memory operand such as [x0, #8]");
    std::int64_t number = 0;
    if (offset) {
        const auto offset_text = text_of(*offset);
        if (parse_register(offset_text) || !modifier_keyword(offset_text).empty()) {
            return mismatch("expected an immediate offset", offset->begin);
        }
        const auto parsed_value = immediate(as, cursor, *offset);
        if (!parsed_value) return Outcome::Abort;
        const auto location = parsed_value->location;
        if (parsed_value->op != Operator::None || !parsed_value->known) {
            // Relocation operators and values that only layout settles: the
            // fields of scaled offsets.
            const auto field = type_field(id, operand.type.memory_bits);
            if (!field) {
                return misfit(location, parsed_value->op == Operator::None
                                            ? "the offset must be known before layout"
                                            : "this memory operand takes no relocation operator");
            }
            if (parsed_value->op != Operator::None &&
                relocation_type(parsed_value->op, *field) == 0) {
                return misfit(location, "relocation operator ':" +
                                            std::string(info(parsed_value->op).name) +
                                            ":' does not apply to this operand");
            }
            value.fixup = PendingFixup{fixup_kind(parsed_value->op, *field),
                                       parsed_value->expression, location};
            value.value = 0;
            next += consumed;
            return Outcome::Parsed;
        }
        number = *parsed_value->known;
        value.location = location;
    } else if (post) {
        return mismatch("expected a post-indexed memory operand such as [x0], #8");
    }
    const std::int64_t scale = id == Id::MemoryScaled || id == Id::Pair || id == Id::PairPre ||
                                       id == Id::PairPost
                                   ? operand.type.memory_bits / 8
                                   : 1;
    const auto minimum = operand.type.offset_minimum;
    const auto maximum = operand.type.offset_maximum;
    const auto field = encoders::scaled(number, scale, minimum, maximum);
    if (!field) {
        if (scale == 1) {
            return misfit(value.location, "offset " + std::to_string(number) +
                                              " is out of range [" + std::to_string(minimum) +
                                              ", " + std::to_string(maximum) + "]");
        }
        return misfit(value.location, "offset " + std::to_string(number) +
                                          " is not a multiple of " + std::to_string(scale) +
                                          " in [" + std::to_string(minimum * scale) + ", " +
                                          std::to_string(maximum * scale) + "]");
    }
    value.value = *field;
    next += consumed;
    return Outcome::Parsed;
}

void Aarch64Target::emit(Assembler& as, const ModelInstruction& entry,
                         const std::vector<OperandValue>& values) {
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
            return;
        }
        const auto mask = field.width >= 32 ? 0xffffffffU : (1U << field.width) - 1U;
        word |= (static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> field.low) & mask)
                << field.shift;
    }
    // A movz (opc 10) whose relocation picks movz or movn.
    if (fixup && info(decode(fixup->kind).first).signed_move &&
        (word & 0x7f800000U) == 0x52800000U) {
        word &= ~(1U << 30);
    }
    emit_word(as, word, fixup);
}

void Aarch64Target::instruction(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    const auto found = forms_.find(name);
    if (found == forms_.end()) {
        as.error(statement.location, "unknown instruction '" + std::string(statement.name) + "'");
        return;
    }
    const auto& cursor = statement.operands;
    const auto items = split_items(cursor.text(), cursor.position(), cursor.text().size());
    // The first form that the CPU has and that takes the operands; else
    // the first that takes their shape reports the values that do not fit,
    // and else the form that takes the most operands reports why the next
    // does not suit it.
    const Form* chosen = nullptr;
    const Form* misfit = nullptr;
    const Form* unavailable = nullptr;
    const Form* closest = nullptr;
    std::size_t closest_items = 0;
    std::vector<OperandValue> values;
    for (const auto& form : found->second) {
        const auto outcome = parse_operands(as, cursor, items, *form.entry, values, false);
        if (outcome == Outcome::Abort) return;
        if (!available(form)) {
            if (outcome != Outcome::Mismatch && !unavailable) unavailable = &form;
            continue;
        }
        if (outcome == Outcome::Parsed) {
            chosen = &form;
            break;
        }
        if (outcome == Outcome::Misfit && !misfit) misfit = &form;
        if (outcome == Outcome::Mismatch && (!closest || failure_ > closest_items)) {
            closest = &form;
            closest_items = failure_;
        }
    }
    if (!chosen) {
        if (!misfit && unavailable) {
            as.error(statement.location, "instruction '" + name + "' needs " + needs(*unavailable));
            return;
        }
        const Form* reporter = misfit ? misfit : closest;
        if (!reporter) {
            as.error(statement.location,
                     "instruction '" + name + "' needs " + needs(found->second.front()));
            return;
        }
        (void)parse_operands(as, cursor, items, *reporter->entry, values, true);
        return;
    }
    const auto& entry = *chosen->entry;
    for (const auto& constraint : entry.constraints) {
        std::int64_t sum = constraint.constant;
        for (const auto& term : constraint.terms) {
            const auto part = values[term.operand].value;
            sum += term.negative ? -part : part;
        }
        if (!constraint.holds(sum)) {
            as.error(values[constraint.terms.back().operand].location,
                     "the operands of '" + name + "' must satisfy " + constraint.text);
            return;
        }
    }
    emit(as, entry, values);
}

bool Aarch64Target::select(Assembler& as, std::string_view text, SourceLocation location,
                           bool extension) {
    const auto& target = subtarget_.target();
    const auto& table = *target.subtargets;
    const auto index_of = [&](std::string_view name) {
        for (std::size_t index = 0; index < table.features.size(); ++index) {
            if (table.features[index].name == name) return index;
        }
        return table.features.size();
    };
    std::unordered_map<std::string, bool> states;
    std::vector<std::pair<std::string, bool>> modifiers;
    if (extension) {
        for (std::size_t index = 0; index < table.features.size(); ++index) {
            states[std::string(table.features[index].name)] = enabled_[index];
        }
        const bool disable = text.starts_with("no") && index_of(text) == table.features.size();
        const auto feature = disable ? text.substr(2) : text;
        if (index_of(feature) == table.features.size() || !table.features[index_of(feature)].selectable) {
            as.error(location, "unknown AArch64 extension '" + std::string(text) + "'");
            return false;
        }
        modifiers.emplace_back(std::string(feature), !disable);
    } else {
        std::string error;
        const auto selection = parse_cpu_selection(target, text, error);
        if (!selection) {
            as.error(location, error.empty() ? "unknown AArch64 architecture or CPU '" +
                                                   std::string(text) + "'"
                                             : error);
            return false;
        }
        disabled_.clear();
        for (const auto& feature : table.features) {
            states[std::string(feature.name)] = feature.enabled_by_default;
        }
        const auto& registry = model_registry();
        const auto* cpu = registry.find_cpu(target.architecture, selection->cpu);
        for (const auto& feature : registry.resolve_cpu(target, *cpu).features) states[feature] = true;
        for (const auto& modifier : selection->modifiers) {
            modifiers.emplace_back(modifier.feature, modifier.enable);
        }
    }
    // As -march modifiers: +FEATURE also selects its explicit selection;
    // +noFEATURE disables its explicit disabling and what requires either.
    for (const auto& [name, enable] : modifiers) {
        const auto& feature = table.features[index_of(name)];
        std::unordered_set<std::string> changed{name};
        for (const auto other : enable ? feature.explicit_on : feature.explicit_off) {
            changed.emplace(other);
        }
        for (bool grew = !enable; grew;) {
            grew = false;
            for (const auto& other : table.features) {
                if (!changed.contains(std::string(other.name)) &&
                    std::any_of(other.prerequisites.begin(), other.prerequisites.end(),
                                [&](std::string_view required) {
                                    return changed.contains(std::string(required));
                                })) {
                    changed.emplace(other.name);
                    grew = true;
                }
            }
        }
        for (const auto& item : changed) {
            states[item] = enable;
            if (enable) disabled_.erase(item);
            else disabled_.insert(item);
        }
    }
    // The enabled features with their prerequisites and implications.
    std::vector<std::string> pending;
    for (const auto& [name, enabled] : states) {
        if (enabled) pending.push_back(name);
    }
    std::vector<bool> result(table.features.size(), false);
    while (!pending.empty()) {
        const auto name = pending.back();
        pending.pop_back();
        const auto index = index_of(name);
        if (index == table.features.size() || result[index]) continue;
        result[index] = true;
        const auto& feature = table.features[index];
        for (const auto other : feature.prerequisites) pending.emplace_back(other);
        for (const auto other : feature.implies) {
            if (!disabled_.contains(std::string(other))) pending.emplace_back(other);
        }
    }
    enabled_ = std::move(result);
    return true;
}

bool Aarch64Target::directive(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    auto& cursor = statement.operands;
    const auto rest = [&]() {
        cursor.skip_space();
        auto text = cursor.rest();
        while (!text.empty() && blank(text.back())) text.remove_suffix(1);
        cursor.set_position(cursor.text().size());
        return std::string(text);
    };
    if (name == ".arch" || name == ".cpu" || name == ".arch_extension") {
        const auto location = cursor.location();
        const auto text = lower(rest());
        if (text.empty()) {
            as.error(location, "expected a name after " + name);
            return true;
        }
        (void)select(as, text, location, name == ".arch_extension");
        return true;
    }
    if (name == ".xword" || name == ".inst") {
        // .xword: 8-byte data; .inst: instruction words.
        if (name == ".xword") map(as, Mapping::Data);
        do {
            cursor.skip_space();
            const auto location = cursor.location();
            const auto expression = as.parse_expression(cursor);
            if (!expression) return true;
            if (name == ".inst") {
                const auto value = as.absolute_now(*expression);
                if (!value || *value < 0 || *value > 0xffffffffLL) {
                    as.error(location, ".inst needs a 32-bit value known before layout");
                    return true;
                }
                emit_word(as, static_cast<std::uint32_t>(*value), std::nullopt);
            } else {
                as.emit_value(*expression, 8, location);
            }
        } while (cursor.consume(","));
        if (!cursor.at_end()) as.error(cursor.location(), "expected ',' between values");
        return true;
    }
    return false;
}

bool Aarch64Target::apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                                std::span<std::uint8_t> bytes, bool) const {
    const auto [op, field] = decode(fixup.kind);
    const auto fail = [&](std::string message) {
        as.error(fixup.location, message);
        return false;
    };
    std::int64_t encoded = 0;
    switch (field) {
    case Field::Branch26:
    case Field::Call26:
    case Field::Branch19:
    case Field::Branch14:
    case Field::Literal19: {
        if ((value & 3) != 0) return fail("the target of a branch or literal is not word aligned");
        const unsigned bits = field == Field::Branch14 ? 14 : field == Field::Branch19 ||
                                                                      field == Field::Literal19
                                                                  ? 19
                                                                  : 26;
        encoded = value / 4;
        if (encoded < -(std::int64_t{1} << (bits - 1)) || encoded >= (std::int64_t{1} << (bits - 1))) {
            return fail(field == Field::Literal19 ? "the literal is out of range"
                                                  : "the branch target is out of range");
        }
        break;
    }
    case Field::Adr21:
        encoded = value;
        if (encoded < -(1 << 20) || encoded >= (1 << 20)) return fail("the adr target is out of range");
        break;
    case Field::Page21:
        return fail("adrp needs a symbol");
    case Field::Add12:
        if (op == Operator::Lo12) {
            encoded = value & 0xfff;
        } else if (op != Operator::None) {
            return fail("relocation operator ':" + std::string(info(op).name) + ":' needs a symbol");
        } else if (value < 0 || value > 4095) {
            return fail("immediate " + std::to_string(value) + " is out of range [0, 4095]");
        } else {
            encoded = value;
        }
        break;
    case Field::Move16:
        if (op == Operator::None) {
            if (value < 0 || value > 0xffff) {
                return fail("immediate " + std::to_string(value) + " is out of range [0, 65535]");
            }
            encoded = value;
        } else if (op >= Operator::AbsG0 && op <= Operator::AbsG3) {
            const auto shift = 16 * info(op).implied;
            const auto unsigned_value = static_cast<std::uint64_t>(value);
            const bool checked = op == Operator::AbsG0 || op == Operator::AbsG1 ||
                                 op == Operator::AbsG2;
            if (checked && (value < 0 || (unsigned_value >> (shift + 16)) != 0)) {
                return fail("the value " + std::to_string(value) + " does not fit :" +
                            std::string(info(op).name) + ":");
            }
            encoded = static_cast<std::int64_t>((unsigned_value >> shift) & 0xffff);
        } else {
            return fail("relocation operator ':" + std::string(info(op).name) + ":' needs a symbol");
        }
        break;
    default: {
        // Scaled offsets: of the value, or of its low 12 bits with :lo12:.
        const auto scale = std::int64_t{1} << scale_log(field);
        if (op != Operator::None && op != Operator::Lo12) {
            return fail("relocation operator ':" + std::string(info(op).name) + ":' needs a symbol");
        }
        const auto offset = op == Operator::Lo12 ? (value & 0xfff) : value;
        const auto quotient = encoders::scaled(offset, scale, 0, 4095);
        if (!quotient) {
            return fail("offset " + std::to_string(offset) + " is not a multiple of " +
                        std::to_string(scale) + " in [0, " + std::to_string(4095 * scale) + "]");
        }
        encoded = *quotient;
        break;
    }
    }
    auto word = static_cast<std::uint32_t>(as.read_integer(bytes));
    for (const auto& slice : relocated_slices(field_type(field))) {
        const auto mask = (std::uint32_t{1} << slice.width) - 1;
        word &= ~(mask << slice.shift);
        word |= (static_cast<std::uint32_t>(static_cast<std::uint64_t>(encoded) >> slice.low) & mask)
                << slice.shift;
    }
    as.write_integer(bytes, word);
    return true;
}

std::optional<assembler::RelocationChoice> Aarch64Target::relocation(
    Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
    const assembler::Symbol&, const assembler::Section*) const {
    using namespace assembler::fixup;
    switch (fixup.kind.value) {
    case data1.value:
        as.error(fixup.location, "AArch64 has no relocation for a 1-byte field");
        return std::nullopt;
    case data2.value: return assembler::RelocationChoice{r_aarch64_abs16};
    case data4.value: return assembler::RelocationChoice{r_aarch64_abs32};
    case data8.value: return assembler::RelocationChoice{r_aarch64_abs64};
    case data4_pc.value: return assembler::RelocationChoice{r_aarch64_prel32};
    case data8_pc.value: return assembler::RelocationChoice{r_aarch64_prel64};
    default: break;
    }
    if (fixup.kind.value < FixupKind::target_base) {
        as.error(fixup.location, "unsupported AArch64 data relocation");
        return std::nullopt;
    }
    const auto [op, field] = decode(fixup.kind);
    const auto type = relocation_type(op, field);
    if (type == 0) {
        as.error(fixup.location, "a symbol in this immediate needs a relocation operator such "
                                 "as :lo12:");
        return std::nullopt;
    }
    // TLS and GOT relocations name their symbol, as LLVM MC's do.
    if (info(op).tls && value.add) as.symbol(*value.add).type = elf::stt_tls;
    return assembler::RelocationChoice{type, info(op).tls || info(op).got};
}

} // namespace

std::optional<std::vector<std::uint8_t>> assemble(const std::vector<const SourceFile*>& sources,
                                                  const Subtarget& subtarget,
                                                  const assembler::Options& options,
                                                  SourceManager& manager,
                                                  Diagnostics& diagnostics) {
    Aarch64Target target(subtarget);
    return assembler::assemble(sources, target, options, manager, diagnostics);
}

} // namespace cross::aarch64
