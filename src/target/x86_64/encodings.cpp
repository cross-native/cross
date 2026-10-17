// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/encodings.hpp"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace cross::x86_64::encoding {
namespace {

using K = Kind;
using L = Layout;
using P = Prefix;
using Mp = Map;
namespace f = flag;

struct ConditionName {
    std::string_view name;
    int code;
};

constexpr ConditionName conditions[] = {
    {"o", 0},   {"no", 1},  {"b", 2},   {"c", 2},   {"nae", 2}, {"ae", 3},  {"nb", 3},
    {"nc", 3},  {"e", 4},   {"z", 4},   {"ne", 5},  {"nz", 5},  {"be", 6},  {"na", 6},
    {"a", 7},   {"nbe", 7}, {"s", 8},   {"ns", 9},  {"p", 10},  {"pe", 10}, {"np", 11},
    {"po", 11}, {"l", 12},  {"nge", 12}, {"ge", 13}, {"nl", 13}, {"le", 14}, {"ng", 14},
    {"g", 15},  {"nle", 15},
};

class Table {
public:
    std::vector<Form> forms;

    std::string_view name(std::string text) {
        names_.push_back(std::move(text));
        return names_.back();
    }

    Form& add(std::string_view mnemonic, std::initializer_list<K> operands, std::uint8_t size,
              Space space, Mp map, P prefix, std::uint8_t opcode, std::int16_t digit, L layout,
              std::uint32_t flags = 0, Tuple tuple = Tuple::None) {
        Form form;
        form.mnemonic = mnemonic;
        std::copy(operands.begin(), operands.end(), form.operands.begin());
        form.size = size;
        form.space = space;
        form.map = map;
        form.prefix = prefix;
        form.opcode = opcode;
        form.digit = digit;
        form.layout = layout;
        form.flags = flags;
        form.tuple = tuple;
        forms.push_back(form);
        return forms.back();
    }

    // A legacy (non-VEX) form.
    Form& legacy(std::string_view mnemonic, std::initializer_list<K> operands, std::uint8_t size,
                 Mp map, P prefix, std::uint8_t opcode, std::int16_t digit, L layout,
                 std::uint32_t flags = 0) {
        return add(mnemonic, operands, size, Space::Legacy, map, prefix, opcode, digit, layout, flags);
    }

    // An opcode-only form without operands.
    void plain(std::string_view mnemonic, Mp map, P prefix, std::uint8_t opcode,
               std::uint32_t flags = 0, std::uint8_t size = 0) {
        legacy(mnemonic, {}, size, map, prefix, opcode, -1, L::None, flags);
    }

    // An opcode followed by a fixed ModRM byte (0F 01 F9, 0F AE E8).
    void fixed(std::string_view mnemonic, Mp map, P prefix, std::uint8_t opcode, std::uint8_t modrm,
               std::uint32_t flags = 0) {
        legacy(mnemonic, {}, 0, map, prefix, opcode, modrm, L::None, flags | f::fixed_modrm);
    }

private:
    std::deque<std::string> names_;
};

K gpr(unsigned size) {
    return size == 8 ? K::R8 : size == 16 ? K::R16 : size == 32 ? K::R32 : K::R64;
}
K rm(unsigned size) {
    return size == 8 ? K::Rm8 : size == 16 ? K::Rm16 : size == 32 ? K::Rm32 : K::Rm64;
}
K immediate(unsigned size) {
    return size == 8 ? K::I8 : size == 16 ? K::I16 : size == 32 ? K::I32 : K::S32;
}
K accumulator(unsigned size) {
    return size == 8 ? K::Al : size == 16 ? K::Ax : size == 32 ? K::Eax : K::Rax;
}
std::uint32_t size_flags(unsigned size) {
    return size == 16 ? f::operand16 : size == 64 ? f::w : 0;
}

constexpr unsigned integer_sizes[] = {8, 16, 32, 64};
constexpr unsigned wide_sizes[] = {16, 32, 64};

void integer(Table& t) {
    // Two-operand arithmetic: short immediate, accumulator, immediate, then
    // register forms, the order LLVM MC's encoding choices follow.
    struct Arithmetic {
        std::string_view name;
        std::uint8_t base;
        std::uint8_t digit;
    };
    for (const auto& [name, base, digit] :
         {Arithmetic{"add", 0x00, 0}, Arithmetic{"or", 0x08, 1}, Arithmetic{"adc", 0x10, 2},
          Arithmetic{"sbb", 0x18, 3}, Arithmetic{"and", 0x20, 4}, Arithmetic{"sub", 0x28, 5},
          Arithmetic{"xor", 0x30, 6}, Arithmetic{"cmp", 0x38, 7}}) {
        const std::uint32_t lock = name == "cmp" ? 0 : f::lock_ok;
        for (const auto size : integer_sizes) {
            const auto s = static_cast<std::uint8_t>(size);
            const auto flags = size_flags(size);
            const std::uint8_t wide = size == 8 ? 0 : 1;
            if (size != 8) t.legacy(name, {rm(size), K::S8}, s, Mp::One, P::None, 0x83, digit, L::M, flags | lock);
            t.legacy(name, {accumulator(size), immediate(size)}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(base + 4 + wide), -1, L::None, flags);
            t.legacy(name, {rm(size), immediate(size)}, s, Mp::One, P::None, size == 8 ? 0x80 : 0x81,
                     digit, L::M, flags | lock);
            t.legacy(name, {rm(size), gpr(size)}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(base + wide), -1, L::MR, flags | lock);
            t.legacy(name, {gpr(size), rm(size)}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(base + 2 + wide), -1, L::RM, flags);
        }
    }
    for (const auto size : integer_sizes) {
        const auto s = static_cast<std::uint8_t>(size);
        const auto flags = size_flags(size);
        const std::uint8_t wide = size == 8 ? 0 : 1;
        t.legacy("test", {accumulator(size), immediate(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0xa8 + wide), -1, L::None, flags);
        t.legacy("test", {rm(size), immediate(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0xf6 + wide), 0, L::M, flags);
        t.legacy("test", {rm(size), gpr(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x84 + wide), -1, L::MR, flags);
        t.legacy("test", {gpr(size), rm(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x84 + wide), -1, L::RM, flags);

        t.legacy("mov", {rm(size), gpr(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x88 + wide), -1, L::MR, flags);
        t.legacy("mov", {gpr(size), rm(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x8a + wide), -1, L::RM, flags);
        if (size == 64) {
            t.legacy("mov", {K::R64, K::S32}, s, Mp::One, P::None, 0xc7, 0, L::M, flags);
            t.legacy("mov", {K::R64, K::I64}, s, Mp::One, P::None, 0xb8, -1, L::O, flags);
            t.legacy("mov", {K::Rm64, K::S32}, s, Mp::One, P::None, 0xc7, 0, L::M, flags);
        } else {
            t.legacy("mov", {gpr(size), immediate(size)}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(size == 8 ? 0xb0 : 0xb8), -1, L::O, flags);
            t.legacy("mov", {rm(size), immediate(size)}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(0xc6 + wide), 0, L::M, flags);
        }
        t.legacy("movabs", {gpr(size), size == 64 ? K::I64 : immediate(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(size == 8 ? 0xb0 : 0xb8), -1, L::O, flags);

        // Unary groups F6/F7 and FE/FF.
        struct Unary {
            std::string_view name;
            std::uint8_t opcode;
            std::uint8_t digit;
            std::uint32_t extra;
        };
        for (const auto& [unary, opcode, digit, extra] :
             {Unary{"inc", 0xfe, 0, f::lock_ok}, Unary{"dec", 0xfe, 1, f::lock_ok},
              Unary{"not", 0xf6, 2, f::lock_ok}, Unary{"neg", 0xf6, 3, f::lock_ok},
              Unary{"mul", 0xf6, 4, 0}, Unary{"imul", 0xf6, 5, 0}, Unary{"div", 0xf6, 6, 0},
              Unary{"idiv", 0xf6, 7, 0}}) {
            t.legacy(unary, {rm(size)}, s, Mp::One, P::None, static_cast<std::uint8_t>(opcode + wide),
                     digit, L::M, flags | extra);
        }

        struct Shift {
            std::string_view name;
            std::uint8_t digit;
        };
        for (const auto& [shift, digit] :
             {Shift{"rol", 0}, Shift{"ror", 1}, Shift{"rcl", 2}, Shift{"rcr", 3}, Shift{"shl", 4},
              Shift{"sal", 4}, Shift{"shr", 5}, Shift{"sar", 7}}) {
            t.legacy(shift, {rm(size), K::One}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(0xd0 + wide), digit, L::M, flags);
            t.legacy(shift, {rm(size), K::Cl}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(0xd2 + wide), digit, L::M, flags);
            t.legacy(shift, {rm(size), K::I8}, s, Mp::One, P::None,
                     static_cast<std::uint8_t>(0xc0 + wide), digit, L::M, flags);
            t.legacy(shift, {rm(size)}, s, Mp::One, P::None, static_cast<std::uint8_t>(0xd0 + wide),
                     digit, L::M, flags);
        }

        // `xchg %acc, %reg` uses the one-byte 0x90+reg form; the matcher
        // tries it before the general forms (and skips the %eax,%eax case,
        // which must not collapse to nop in 64-bit mode).
        if (size != 8) {
            t.legacy("xchg", {accumulator(size), gpr(size)}, s, Mp::One, P::None, 0x90, -1, L::O, flags);
            t.legacy("xchg", {gpr(size), accumulator(size)}, s, Mp::One, P::None, 0x90, -1, L::O, flags);
        }
        t.legacy("xchg", {gpr(size), rm(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x86 + wide), -1, L::RM, flags | f::lock_ok);
        t.legacy("xchg", {rm(size), gpr(size)}, s, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x86 + wide), -1, L::MR, flags | f::lock_ok);
        t.legacy("xadd", {rm(size), gpr(size)}, s, Mp::M0F, P::None,
                 static_cast<std::uint8_t>(0xc0 + wide), -1, L::MR, flags | f::lock_ok);
        t.legacy("cmpxchg", {rm(size), gpr(size)}, s, Mp::M0F, P::None,
                 static_cast<std::uint8_t>(0xb0 + wide), -1, L::MR, flags | f::lock_ok);
    }

    for (const auto size : wide_sizes) {
        const auto s = static_cast<std::uint8_t>(size);
        const auto flags = size_flags(size);
        t.legacy("lea", {gpr(size), K::Mem}, s, Mp::One, P::None, 0x8d, -1, L::RM, flags);
        t.legacy("imul", {gpr(size), rm(size)}, s, Mp::M0F, P::None, 0xaf, -1, L::RM, flags);
        t.legacy("imul", {gpr(size), rm(size), K::S8}, s, Mp::One, P::None, 0x6b, -1, L::RM, flags);
        t.legacy("imul", {gpr(size), rm(size), immediate(size)}, s, Mp::One, P::None, 0x69, -1, L::RM, flags);
        for (const auto& [name, opcode] : {std::pair{std::string_view{"shld"}, std::uint8_t{0xa4}},
                                           std::pair{std::string_view{"shrd"}, std::uint8_t{0xac}}}) {
            t.legacy(name, {rm(size), gpr(size), K::I8}, s, Mp::M0F, P::None, opcode, -1, L::MR, flags);
            t.legacy(name, {rm(size), gpr(size), K::Cl}, s, Mp::M0F, P::None,
                     static_cast<std::uint8_t>(opcode + 1), -1, L::MR, flags);
            t.legacy(name, {rm(size), gpr(size)}, s, Mp::M0F, P::None,
                     static_cast<std::uint8_t>(opcode + 1), -1, L::MR, flags);
        }
        struct BitTest {
            std::string_view name;
            std::uint8_t opcode;
            std::uint8_t digit;
            std::uint32_t extra;
        };
        for (const auto& [name, opcode, digit, extra] :
             {BitTest{"bt", 0xa3, 4, 0}, BitTest{"bts", 0xab, 5, f::lock_ok},
              BitTest{"btr", 0xb3, 6, f::lock_ok}, BitTest{"btc", 0xbb, 7, f::lock_ok}}) {
            t.legacy(name, {rm(size), gpr(size)}, s, Mp::M0F, P::None, opcode, -1, L::MR, flags | extra);
            t.legacy(name, {rm(size), K::I8}, s, Mp::M0F, P::None, 0xba, digit, L::M, flags | extra);
        }
        t.legacy("bsf", {gpr(size), rm(size)}, s, Mp::M0F, P::None, 0xbc, -1, L::RM, flags);
        t.legacy("bsr", {gpr(size), rm(size)}, s, Mp::M0F, P::None, 0xbd, -1, L::RM, flags);
        t.legacy("tzcnt", {gpr(size), rm(size)}, s, Mp::M0F, P::PF3, 0xbc, -1, L::RM, flags);
        t.legacy("lzcnt", {gpr(size), rm(size)}, s, Mp::M0F, P::PF3, 0xbd, -1, L::RM, flags);
        t.legacy("popcnt", {gpr(size), rm(size)}, s, Mp::M0F, P::PF3, 0xb8, -1, L::RM, flags);
        t.legacy("movbe", {gpr(size), rm(size)}, s, Mp::M0F38, P::None, 0xf0, -1, L::RM, flags);
        t.legacy("movbe", {rm(size), gpr(size)}, s, Mp::M0F38, P::None, 0xf1, -1, L::MR, flags);
        for (const auto& [condition, code] : conditions) {
            t.legacy(t.name("cmov" + std::string(condition)), {gpr(size), rm(size)}, s, Mp::M0F, P::None,
                     static_cast<std::uint8_t>(0x40 + code), -1, L::RM, flags);
        }
        t.legacy("rdrand", {gpr(size)}, s, Mp::M0F, P::None, 0xc7, 6, L::M, flags);
        t.legacy("rdseed", {gpr(size)}, s, Mp::M0F, P::None, 0xc7, 7, L::M, flags);
        // Pushes and pops of 16-bit operands need the suffix; 64 bits is
        // the default in 64-bit mode.
    }
    for (const auto size : {32U, 64U}) {
        const auto s = static_cast<std::uint8_t>(size);
        const auto flags = size_flags(size);
        t.legacy("bswap", {gpr(size)}, s, Mp::M0F, P::None, 0xc8, -1, L::O, flags);
        t.legacy("adcx", {gpr(size), rm(size)}, s, Mp::M0F38, P::P66, 0xf6, -1, L::RM, flags);
        t.legacy("adox", {gpr(size), rm(size)}, s, Mp::M0F38, P::PF3, 0xf6, -1, L::RM, flags);
        t.legacy("rdfsbase", {gpr(size)}, s, Mp::M0F, P::PF3, 0xae, 0, L::M, flags);
        t.legacy("rdgsbase", {gpr(size)}, s, Mp::M0F, P::PF3, 0xae, 1, L::M, flags);
        t.legacy("wrfsbase", {gpr(size)}, s, Mp::M0F, P::PF3, 0xae, 2, L::M, flags);
        t.legacy("wrgsbase", {gpr(size)}, s, Mp::M0F, P::PF3, 0xae, 3, L::M, flags);
        t.legacy("movdiri", {size == 32 ? K::M32 : K::M64, gpr(size)}, s, Mp::M0F38, P::None, 0xf9, -1,
                 L::MR, flags);
    }
    t.legacy("crc32b", {K::R32, K::Rm8}, 0, Mp::M0F38, P::PF2, 0xf0, -1, L::RM);
    t.legacy("crc32b", {K::R64, K::Rm8}, 0, Mp::M0F38, P::PF2, 0xf0, -1, L::RM, f::w);
    t.legacy("crc32w", {K::R32, K::Rm16}, 0, Mp::M0F38, P::PF2, 0xf1, -1, L::RM, f::operand16);
    t.legacy("crc32l", {K::R32, K::Rm32}, 0, Mp::M0F38, P::PF2, 0xf1, -1, L::RM);
    t.legacy("crc32q", {K::R64, K::Rm64}, 0, Mp::M0F38, P::PF2, 0xf1, -1, L::RM, f::w);

    // Zero and sign extension. The AT&T names carry both sizes.
    struct Extend {
        std::string_view name;
        K destination;
        K source;
        Mp map;
        std::uint8_t opcode;
        std::uint32_t flags;
    };
    for (const auto& [name, destination, source, map, opcode, flags] :
         {Extend{"movzbw", K::R16, K::Rm8, Mp::M0F, 0xb6, f::operand16},
          Extend{"movzbl", K::R32, K::Rm8, Mp::M0F, 0xb6, 0},
          Extend{"movzbq", K::R64, K::Rm8, Mp::M0F, 0xb6, f::w},
          Extend{"movzwl", K::R32, K::Rm16, Mp::M0F, 0xb7, 0},
          Extend{"movzwq", K::R64, K::Rm16, Mp::M0F, 0xb7, f::w},
          Extend{"movsbw", K::R16, K::Rm8, Mp::M0F, 0xbe, f::operand16},
          Extend{"movsbl", K::R32, K::Rm8, Mp::M0F, 0xbe, 0},
          Extend{"movsbq", K::R64, K::Rm8, Mp::M0F, 0xbe, f::w},
          Extend{"movswl", K::R32, K::Rm16, Mp::M0F, 0xbf, 0},
          Extend{"movswq", K::R64, K::Rm16, Mp::M0F, 0xbf, f::w},
          Extend{"movslq", K::R64, K::Rm32, Mp::One, 0x63, f::w},
          Extend{"movsxd", K::R64, K::Rm32, Mp::One, 0x63, f::w},
          Extend{"movzx", K::R16, K::Rm8, Mp::M0F, 0xb6, f::operand16},
          Extend{"movzx", K::R32, K::Rm8, Mp::M0F, 0xb6, 0},
          Extend{"movzx", K::R64, K::Rm8, Mp::M0F, 0xb6, f::w},
          Extend{"movzx", K::R32, K::Rm16, Mp::M0F, 0xb7, 0},
          Extend{"movzx", K::R64, K::Rm16, Mp::M0F, 0xb7, f::w},
          Extend{"movsx", K::R16, K::Rm8, Mp::M0F, 0xbe, f::operand16},
          Extend{"movsx", K::R32, K::Rm8, Mp::M0F, 0xbe, 0},
          Extend{"movsx", K::R64, K::Rm8, Mp::M0F, 0xbe, f::w},
          Extend{"movsx", K::R32, K::Rm16, Mp::M0F, 0xbf, 0},
          Extend{"movsx", K::R64, K::Rm16, Mp::M0F, 0xbf, f::w}}) {
        t.legacy(name, {destination, source}, 0, map, P::None, opcode, -1, L::RM, flags);
    }

    // Control transfer.
    for (const auto& [condition, code] : conditions) {
        t.legacy(t.name("j" + std::string(condition)), {K::Rel8}, 0, Mp::One, P::None,
                 static_cast<std::uint8_t>(0x70 + code), -1, L::D, f::relaxable);
        t.legacy(t.name("set" + std::string(condition)), {K::Rm8}, 0, Mp::M0F, P::None,
                 static_cast<std::uint8_t>(0x90 + code), 0, L::M);
    }
    t.legacy("jmp", {K::Rel8}, 0, Mp::One, P::None, 0xeb, -1, L::D, f::relaxable);
    t.legacy("jmp", {K::Rm64}, 64, Mp::One, P::None, 0xff, 4, L::M);
    t.legacy("call", {K::Rel32}, 64, Mp::One, P::None, 0xe8, -1, L::D);
    t.legacy("call", {K::Rm64}, 64, Mp::One, P::None, 0xff, 2, L::M);
    t.legacy("jrcxz", {K::Rel8}, 0, Mp::One, P::None, 0xe3, -1, L::D);
    t.legacy("loop", {K::Rel8}, 0, Mp::One, P::None, 0xe2, -1, L::D);
    t.legacy("loope", {K::Rel8}, 0, Mp::One, P::None, 0xe1, -1, L::D);
    t.legacy("loopne", {K::Rel8}, 0, Mp::One, P::None, 0xe0, -1, L::D);
    t.legacy("ret", {}, 64, Mp::One, P::None, 0xc3, -1, L::None);
    t.legacy("ret", {K::I16}, 64, Mp::One, P::None, 0xc2, -1, L::None);
    t.legacy("ret", {}, 16, Mp::One, P::None, 0xc3, -1, L::None, f::operand16 | f::no_rex_w_default);
    t.legacy("ret", {K::I16}, 16, Mp::One, P::None, 0xc2, -1, L::None, f::operand16 | f::no_rex_w_default);
    t.legacy("lret", {}, 32, Mp::One, P::None, 0xcb, -1, L::None);
    t.legacy("lret", {}, 64, Mp::One, P::None, 0xcb, -1, L::None, f::w);
    t.legacy("push", {K::R64}, 64, Mp::One, P::None, 0x50, -1, L::O);
    t.legacy("push", {K::Rm64}, 64, Mp::One, P::None, 0xff, 6, L::M);
    t.legacy("push", {K::S8}, 64, Mp::One, P::None, 0x6a, -1, L::None);
    t.legacy("push", {K::S32}, 64, Mp::One, P::None, 0x68, -1, L::None);
    t.legacy("push", {K::R16}, 16, Mp::One, P::None, 0x50, -1, L::O, f::operand16 | f::no_rex_w_default);
    t.legacy("push", {K::Rm16}, 16, Mp::One, P::None, 0xff, 6, L::M, f::operand16 | f::no_rex_w_default);
    t.legacy("push", {K::S8}, 16, Mp::One, P::None, 0x6a, -1, L::None, f::operand16 | f::no_rex_w_default);
    t.legacy("push", {K::I16}, 16, Mp::One, P::None, 0x68, -1, L::None, f::operand16 | f::no_rex_w_default);
    t.legacy("pop", {K::R64}, 64, Mp::One, P::None, 0x58, -1, L::O);
    t.legacy("pop", {K::Rm64}, 64, Mp::One, P::None, 0x8f, 0, L::M);
    t.legacy("pop", {K::R16}, 16, Mp::One, P::None, 0x58, -1, L::O, f::operand16 | f::no_rex_w_default);
    t.legacy("pop", {K::Rm16}, 16, Mp::One, P::None, 0x8f, 0, L::M, f::operand16 | f::no_rex_w_default);
    t.legacy("enter", {K::I16, K::I8}, 64, Mp::One, P::None, 0xc8, -1, L::None);
    t.plain("leave", Mp::One, P::None, 0xc9, 0, 64);
    t.plain("pushf", Mp::One, P::None, 0x9c, 0, 64);
    t.plain("popf", Mp::One, P::None, 0x9d, 0, 64);
    t.plain("pushfq", Mp::One, P::None, 0x9c);
    t.plain("popfq", Mp::One, P::None, 0x9d);

    t.legacy("cmpxchg8b", {K::M64}, 0, Mp::M0F, P::None, 0xc7, 1, L::M, f::lock_ok);
    t.legacy("cmpxchg16b", {K::M128}, 0, Mp::M0F, P::None, 0xc7, 1, L::M, f::w | f::lock_ok);

    // Sign extension of the accumulator, in AT&T and Intel spellings.
    for (const auto& [name, opcode, flags] :
         {std::tuple{std::string_view{"cbtw"}, std::uint8_t{0x98}, f::operand16},
          std::tuple{std::string_view{"cbw"}, std::uint8_t{0x98}, f::operand16},
          std::tuple{std::string_view{"cwtl"}, std::uint8_t{0x98}, 0U},
          std::tuple{std::string_view{"cwde"}, std::uint8_t{0x98}, 0U},
          std::tuple{std::string_view{"cltq"}, std::uint8_t{0x98}, f::w},
          std::tuple{std::string_view{"cdqe"}, std::uint8_t{0x98}, f::w},
          std::tuple{std::string_view{"cwtd"}, std::uint8_t{0x99}, f::operand16},
          std::tuple{std::string_view{"cwd"}, std::uint8_t{0x99}, f::operand16},
          std::tuple{std::string_view{"cltd"}, std::uint8_t{0x99}, 0U},
          std::tuple{std::string_view{"cdq"}, std::uint8_t{0x99}, 0U},
          std::tuple{std::string_view{"cqto"}, std::uint8_t{0x99}, f::w},
          std::tuple{std::string_view{"cqo"}, std::uint8_t{0x99}, f::w}}) {
        t.plain(name, Mp::One, P::None, opcode, flags);
    }

    // String instructions.
    struct StringOp {
        std::string_view name;
        std::uint8_t opcode;
    };
    for (const auto& [name, opcode] :
         {StringOp{"movs", 0xa4}, StringOp{"cmps", 0xa6}, StringOp{"stos", 0xaa},
          StringOp{"lods", 0xac}, StringOp{"scas", 0xae}}) {
        t.plain(t.name(std::string(name) + "b"), Mp::One, P::None, opcode);
        t.plain(t.name(std::string(name) + "w"), Mp::One, P::None, static_cast<std::uint8_t>(opcode + 1), f::operand16);
        t.plain(t.name(std::string(name) + "l"), Mp::One, P::None, static_cast<std::uint8_t>(opcode + 1));
        t.plain(t.name(std::string(name) + "q"), Mp::One, P::None, static_cast<std::uint8_t>(opcode + 1), f::w);
    }

    // Flags, synchronization, and system instructions.
    for (const auto& [name, opcode] :
         {std::pair{std::string_view{"clc"}, std::uint8_t{0xf8}}, std::pair{std::string_view{"stc"}, std::uint8_t{0xf9}},
          std::pair{std::string_view{"cmc"}, std::uint8_t{0xf5}}, std::pair{std::string_view{"cld"}, std::uint8_t{0xfc}},
          std::pair{std::string_view{"std"}, std::uint8_t{0xfd}}, std::pair{std::string_view{"cli"}, std::uint8_t{0xfa}},
          std::pair{std::string_view{"sti"}, std::uint8_t{0xfb}}, std::pair{std::string_view{"sahf"}, std::uint8_t{0x9e}},
          std::pair{std::string_view{"lahf"}, std::uint8_t{0x9f}}, std::pair{std::string_view{"hlt"}, std::uint8_t{0xf4}},
          std::pair{std::string_view{"int3"}, std::uint8_t{0xcc}}, std::pair{std::string_view{"nop"}, std::uint8_t{0x90}},
          std::pair{std::string_view{"wait"}, std::uint8_t{0x9b}}, std::pair{std::string_view{"fwait"}, std::uint8_t{0x9b}}}) {
        t.plain(name, Mp::One, P::None, opcode);
    }
    t.legacy("int", {K::I8}, 0, Mp::One, P::None, 0xcd, -1, L::None);
    t.legacy("nop", {K::Rm16}, 16, Mp::M0F, P::None, 0x1f, 0, L::M, f::operand16);
    t.legacy("nop", {K::Rm32}, 32, Mp::M0F, P::None, 0x1f, 0, L::M);
    t.legacy("nop", {K::Rm64}, 64, Mp::M0F, P::None, 0x1f, 0, L::M, f::w);
    t.plain("pause", Mp::One, P::PF3, 0x90);
    for (const auto& [name, opcode] :
         {std::pair{std::string_view{"ud2"}, std::uint8_t{0x0b}}, std::pair{std::string_view{"cpuid"}, std::uint8_t{0xa2}},
          std::pair{std::string_view{"rdtsc"}, std::uint8_t{0x31}}, std::pair{std::string_view{"rdpmc"}, std::uint8_t{0x33}},
          std::pair{std::string_view{"rdmsr"}, std::uint8_t{0x32}}, std::pair{std::string_view{"wrmsr"}, std::uint8_t{0x30}},
          std::pair{std::string_view{"syscall"}, std::uint8_t{0x05}}, std::pair{std::string_view{"sysret"}, std::uint8_t{0x07}},
          std::pair{std::string_view{"wbinvd"}, std::uint8_t{0x09}}, std::pair{std::string_view{"invd"}, std::uint8_t{0x08}},
          std::pair{std::string_view{"emms"}, std::uint8_t{0x77}}, std::pair{std::string_view{"clts"}, std::uint8_t{0x06}}}) {
        t.plain(name, Mp::M0F, P::None, opcode);
    }
    t.plain("wbnoinvd", Mp::M0F, P::PF3, 0x09);
    t.plain("sysretq", Mp::M0F, P::None, 0x07, f::w);
    t.fixed("rdtscp", Mp::M0F, P::None, 0x01, 0xf9);
    t.fixed("lfence", Mp::M0F, P::None, 0xae, 0xe8);
    t.fixed("mfence", Mp::M0F, P::None, 0xae, 0xf0);
    t.fixed("sfence", Mp::M0F, P::None, 0xae, 0xf8);
    t.fixed("xgetbv", Mp::M0F, P::None, 0x01, 0xd0);
    t.fixed("xsetbv", Mp::M0F, P::None, 0x01, 0xd1);
    t.fixed("rdpkru", Mp::M0F, P::None, 0x01, 0xee);
    t.fixed("wrpkru", Mp::M0F, P::None, 0x01, 0xef);
    t.fixed("monitor", Mp::M0F, P::None, 0x01, 0xc8);
    t.fixed("mwait", Mp::M0F, P::None, 0x01, 0xc9);
    t.fixed("serialize", Mp::M0F, P::None, 0x01, 0xe8);
    t.fixed("clac", Mp::M0F, P::None, 0x01, 0xca);
    t.fixed("stac", Mp::M0F, P::None, 0x01, 0xcb);
    t.fixed("swapgs", Mp::M0F, P::None, 0x01, 0xf8);
    t.fixed("endbr64", Mp::M0F, P::PF3, 0x1e, 0xfa);
    t.fixed("endbr32", Mp::M0F, P::PF3, 0x1e, 0xfb);
    // Implicit-register spellings of monitor and mwait.
    t.legacy("monitor", {K::Rax, K::None}, 0, Mp::M0F, P::None, 0x01, 0xc8, L::None, f::fixed_modrm);
    t.legacy("rdpid", {K::R64}, 0, Mp::M0F, P::PF3, 0xc7, 7, L::M);
    t.legacy("umonitor", {K::R64}, 0, Mp::M0F, P::PF3, 0xae, 6, L::M);
    t.legacy("umwait", {K::R32}, 0, Mp::M0F, P::PF2, 0xae, 6, L::M);
    t.legacy("tpause", {K::R32}, 0, Mp::M0F, P::P66, 0xae, 6, L::M);
    t.legacy("invpcid", {K::R64, K::M128}, 0, Mp::M0F38, P::P66, 0x82, -1, L::RM);
    t.legacy("clflush", {K::M8}, 0, Mp::M0F, P::None, 0xae, 7, L::M);
    t.legacy("clflushopt", {K::M8}, 0, Mp::M0F, P::P66, 0xae, 7, L::M);
    t.legacy("clwb", {K::M8}, 0, Mp::M0F, P::P66, 0xae, 6, L::M);
    t.legacy("cldemote", {K::M8}, 0, Mp::M0F, P::None, 0x1c, 0, L::M);
    t.legacy("prefetchnta", {K::M8}, 0, Mp::M0F, P::None, 0x18, 0, L::M);
    t.legacy("prefetcht0", {K::M8}, 0, Mp::M0F, P::None, 0x18, 1, L::M);
    t.legacy("prefetcht1", {K::M8}, 0, Mp::M0F, P::None, 0x18, 2, L::M);
    t.legacy("prefetcht2", {K::M8}, 0, Mp::M0F, P::None, 0x18, 3, L::M);
    t.legacy("prefetchw", {K::M8}, 0, Mp::M0F, P::None, 0x0d, 1, L::M);
    t.legacy("enqcmd", {K::R64, K::M512}, 0, Mp::M0F38, P::PF2, 0xf8, -1, L::RM);
    t.legacy("enqcmds", {K::R64, K::M512}, 0, Mp::M0F38, P::PF3, 0xf8, -1, L::RM);
    t.legacy("movdir64b", {K::R64, K::M512}, 0, Mp::M0F38, P::P66, 0xf8, -1, L::RM);
    t.legacy("ldmxcsr", {K::M32}, 0, Mp::M0F, P::None, 0xae, 2, L::M);
    t.legacy("stmxcsr", {K::M32}, 0, Mp::M0F, P::None, 0xae, 3, L::M);
    struct Save {
        std::string_view name;
        std::uint8_t opcode;
        std::uint8_t digit;
    };
    for (const auto& [name, opcode, digit] :
         {Save{"fxsave", 0xae, 0}, Save{"fxrstor", 0xae, 1}, Save{"xsave", 0xae, 4},
          Save{"xrstor", 0xae, 5}, Save{"xsaveopt", 0xae, 6}, Save{"xsavec", 0xc7, 4},
          Save{"xsaves", 0xc7, 5}, Save{"xrstors", 0xc7, 3}}) {
        t.legacy(name, {K::Mem}, 0, Mp::M0F, P::None, opcode, digit, L::M);
        t.legacy(t.name(std::string(name) + "64"), {K::Mem}, 0, Mp::M0F, P::None, opcode, digit, L::M, f::w);
    }
}

void x87(Table& t) {
    const auto legacy = [&](std::string_view name, std::initializer_list<K> operands, std::uint8_t opcode,
                            std::int16_t digit, L layout, std::uint32_t flags = 0) {
        t.legacy(name, operands, 0, Mp::One, P::None, opcode, digit, layout, flags);
    };
    // Register forms put the stack index into the ModRM byte `digit`.
    const auto stack = [&](std::string_view name, std::initializer_list<K> operands, std::uint8_t opcode,
                           std::uint8_t modrm) {
        t.legacy(name, operands, 0, Mp::One, P::None, opcode, modrm, L::None, f::fixed_modrm);
    };
    const auto bare = [&](std::string_view name, std::uint8_t opcode, std::uint8_t modrm) {
        t.fixed(name, Mp::One, P::None, opcode, modrm);
    };
    // Arithmetic with the AT&T reading of the reversed DC/DE forms.
    struct Arithmetic {
        std::string_view name;
        std::uint8_t digit;
    };
    for (const auto& [name, digit] :
         {Arithmetic{"add", 0}, Arithmetic{"mul", 1}, Arithmetic{"sub", 4}, Arithmetic{"subr", 5},
          Arithmetic{"div", 6}, Arithmetic{"divr", 7}}) {
        const std::string base = "f" + std::string(name);
        const auto modrm = static_cast<std::uint8_t>(0xc0 + digit * 8);
        legacy(t.name(base + "s"), {K::M32}, 0xd8, digit, L::M);
        legacy(t.name(base + "l"), {K::M64}, 0xdc, digit, L::M);
        legacy(t.name("fi" + std::string(name) + "s"), {K::M16}, 0xde, digit, L::M);
        legacy(t.name("fi" + std::string(name) + "l"), {K::M32}, 0xda, digit, L::M);
        stack(t.name(base), {K::St0, K::St}, 0xd8, modrm);
        stack(t.name(base), {K::St, K::St0}, 0xdc, modrm);
        stack(t.name(base + "p"), {K::St, K::St0}, 0xde, modrm);
        stack(t.name(base + "p"), {K::St}, 0xde, modrm);
        bare(t.name(base + "p"), 0xde, static_cast<std::uint8_t>(modrm + 1));
    }
    legacy("fcoms", {K::M32}, 0xd8, 2, L::M);
    legacy("fcoml", {K::M64}, 0xdc, 2, L::M);
    legacy("fcomps", {K::M32}, 0xd8, 3, L::M);
    legacy("fcompl", {K::M64}, 0xdc, 3, L::M);
    stack("fcom", {K::St}, 0xd8, 0xd0);
    bare("fcom", 0xd8, 0xd1);
    stack("fcomp", {K::St}, 0xd8, 0xd8);
    bare("fcomp", 0xd8, 0xd9);
    bare("fcompp", 0xde, 0xd9);
    stack("fucom", {K::St}, 0xdd, 0xe0);
    bare("fucom", 0xdd, 0xe1);
    stack("fucomp", {K::St}, 0xdd, 0xe8);
    bare("fucomp", 0xdd, 0xe9);
    bare("fucompp", 0xda, 0xe9);
    for (const auto& [name, opcode, modrm] :
         {std::tuple{std::string_view{"fcomi"}, std::uint8_t{0xdb}, std::uint8_t{0xf0}},
          std::tuple{std::string_view{"fcomip"}, std::uint8_t{0xdf}, std::uint8_t{0xf0}},
          std::tuple{std::string_view{"fcompi"}, std::uint8_t{0xdf}, std::uint8_t{0xf0}},
          std::tuple{std::string_view{"fucomi"}, std::uint8_t{0xdb}, std::uint8_t{0xe8}},
          std::tuple{std::string_view{"fucomip"}, std::uint8_t{0xdf}, std::uint8_t{0xe8}},
          std::tuple{std::string_view{"fucompi"}, std::uint8_t{0xdf}, std::uint8_t{0xe8}},
          std::tuple{std::string_view{"fcmovb"}, std::uint8_t{0xda}, std::uint8_t{0xc0}},
          std::tuple{std::string_view{"fcmove"}, std::uint8_t{0xda}, std::uint8_t{0xc8}},
          std::tuple{std::string_view{"fcmovbe"}, std::uint8_t{0xda}, std::uint8_t{0xd0}},
          std::tuple{std::string_view{"fcmovu"}, std::uint8_t{0xda}, std::uint8_t{0xd8}},
          std::tuple{std::string_view{"fcmovnb"}, std::uint8_t{0xdb}, std::uint8_t{0xc0}},
          std::tuple{std::string_view{"fcmovne"}, std::uint8_t{0xdb}, std::uint8_t{0xc8}},
          std::tuple{std::string_view{"fcmovnbe"}, std::uint8_t{0xdb}, std::uint8_t{0xd0}},
          std::tuple{std::string_view{"fcmovnu"}, std::uint8_t{0xdb}, std::uint8_t{0xd8}}}) {
        stack(name, {K::St0, K::St}, opcode, modrm);
        stack(name, {K::St}, opcode, modrm);
    }
    legacy("flds", {K::M32}, 0xd9, 0, L::M);
    legacy("fldl", {K::M64}, 0xdd, 0, L::M);
    legacy("fldt", {K::M80}, 0xdb, 5, L::M);
    stack("fld", {K::St}, 0xd9, 0xc0);
    legacy("fsts", {K::M32}, 0xd9, 2, L::M);
    legacy("fstl", {K::M64}, 0xdd, 2, L::M);
    stack("fst", {K::St}, 0xdd, 0xd0);
    legacy("fstps", {K::M32}, 0xd9, 3, L::M);
    legacy("fstpl", {K::M64}, 0xdd, 3, L::M);
    legacy("fstpt", {K::M80}, 0xdb, 7, L::M);
    stack("fstp", {K::St}, 0xdd, 0xd8);
    legacy("filds", {K::M16}, 0xdf, 0, L::M);
    legacy("fildl", {K::M32}, 0xdb, 0, L::M);
    legacy("fildll", {K::M64}, 0xdf, 5, L::M);
    legacy("fildq", {K::M64}, 0xdf, 5, L::M);
    legacy("fists", {K::M16}, 0xdf, 2, L::M);
    legacy("fistl", {K::M32}, 0xdb, 2, L::M);
    legacy("fistps", {K::M16}, 0xdf, 3, L::M);
    legacy("fistpl", {K::M32}, 0xdb, 3, L::M);
    legacy("fistpll", {K::M64}, 0xdf, 7, L::M);
    legacy("fistpq", {K::M64}, 0xdf, 7, L::M);
    legacy("fisttps", {K::M16}, 0xdf, 1, L::M);
    legacy("fisttpl", {K::M32}, 0xdb, 1, L::M);
    legacy("fisttpll", {K::M64}, 0xdd, 1, L::M);
    legacy("fisttpq", {K::M64}, 0xdd, 1, L::M);
    stack("fxch", {K::St}, 0xd9, 0xc8);
    bare("fxch", 0xd9, 0xc9);
    stack("ffree", {K::St}, 0xdd, 0xc0);
    for (const auto& [name, modrm] :
         {std::pair{std::string_view{"fchs"}, std::uint8_t{0xe0}}, std::pair{std::string_view{"fabs"}, std::uint8_t{0xe1}},
          std::pair{std::string_view{"ftst"}, std::uint8_t{0xe4}}, std::pair{std::string_view{"fxam"}, std::uint8_t{0xe5}},
          std::pair{std::string_view{"fld1"}, std::uint8_t{0xe8}}, std::pair{std::string_view{"fldl2t"}, std::uint8_t{0xe9}},
          std::pair{std::string_view{"fldl2e"}, std::uint8_t{0xea}}, std::pair{std::string_view{"fldpi"}, std::uint8_t{0xeb}},
          std::pair{std::string_view{"fldlg2"}, std::uint8_t{0xec}}, std::pair{std::string_view{"fldln2"}, std::uint8_t{0xed}},
          std::pair{std::string_view{"fldz"}, std::uint8_t{0xee}}, std::pair{std::string_view{"f2xm1"}, std::uint8_t{0xf0}},
          std::pair{std::string_view{"fyl2x"}, std::uint8_t{0xf1}}, std::pair{std::string_view{"fptan"}, std::uint8_t{0xf2}},
          std::pair{std::string_view{"fpatan"}, std::uint8_t{0xf3}}, std::pair{std::string_view{"fxtract"}, std::uint8_t{0xf4}},
          std::pair{std::string_view{"fprem1"}, std::uint8_t{0xf5}}, std::pair{std::string_view{"fdecstp"}, std::uint8_t{0xf6}},
          std::pair{std::string_view{"fincstp"}, std::uint8_t{0xf7}}, std::pair{std::string_view{"fprem"}, std::uint8_t{0xf8}},
          std::pair{std::string_view{"fyl2xp1"}, std::uint8_t{0xf9}}, std::pair{std::string_view{"fsqrt"}, std::uint8_t{0xfa}},
          std::pair{std::string_view{"fsincos"}, std::uint8_t{0xfb}}, std::pair{std::string_view{"frndint"}, std::uint8_t{0xfc}},
          std::pair{std::string_view{"fscale"}, std::uint8_t{0xfd}}, std::pair{std::string_view{"fsin"}, std::uint8_t{0xfe}},
          std::pair{std::string_view{"fcos"}, std::uint8_t{0xff}}, std::pair{std::string_view{"fnop"}, std::uint8_t{0xd0}}}) {
        bare(name, 0xd9, modrm);
    }
    legacy("fldcw", {K::M16}, 0xd9, 5, L::M);
    legacy("fnstcw", {K::M16}, 0xd9, 7, L::M);
    legacy("fldenv", {K::Mem}, 0xd9, 4, L::M);
    legacy("fnstenv", {K::Mem}, 0xd9, 6, L::M);
    legacy("frstor", {K::Mem}, 0xdd, 4, L::M);
    legacy("fnsave", {K::Mem}, 0xdd, 6, L::M);
    legacy("fnstsw", {K::M16}, 0xdd, 7, L::M);
    t.legacy("fnstsw", {K::Ax}, 0, Mp::One, P::None, 0xdf, 0xe0, L::None, f::fixed_modrm);
    bare("fnstsw", 0xdf, 0xe0);
    bare("fninit", 0xdb, 0xe3);
    bare("fnclex", 0xdb, 0xe2);
}

} // namespace

void vector_forms(std::vector<Form>& forms, std::deque<std::string>& names);

namespace {

struct Index {
    std::vector<Form> forms;
    std::unordered_map<std::string_view, std::pair<std::size_t, std::size_t>> by_mnemonic;
};

const Index& index() {
    static const Index table = [] {
        Table t;
        integer(t);
        x87(t);
        static std::deque<std::string> vector_names;
        vector_forms(t.forms, vector_names);
        Index result;
        result.forms = std::move(t.forms);
        std::stable_sort(result.forms.begin(), result.forms.end(),
                         [](const Form& a, const Form& b) { return a.mnemonic < b.mnemonic; });
        for (std::size_t begin = 0; begin < result.forms.size();) {
            std::size_t end = begin;
            while (end < result.forms.size() && result.forms[end].mnemonic == result.forms[begin].mnemonic) ++end;
            result.by_mnemonic.emplace(result.forms[begin].mnemonic, std::pair{begin, end});
            begin = end;
        }
        // The names live in the Table's storage; keep it alive.
        static Table keep = std::move(t);
        return result;
    }();
    return table;
}

} // namespace

std::span<const Form> forms(std::string_view mnemonic) {
    const auto& table = index();
    const auto found = table.by_mnemonic.find(mnemonic);
    if (found == table.by_mnemonic.end()) return {};
    return std::span(table.forms).subspan(found->second.first, found->second.second - found->second.first);
}

int condition_code(std::string_view suffix) {
    for (const auto& [name, code] : conditions) {
        if (name == suffix) return code;
    }
    return -1;
}

} // namespace cross::x86_64::encoding
